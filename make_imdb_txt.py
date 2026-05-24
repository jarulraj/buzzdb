#!/usr/bin/env python3
"""
Generate course-sized v47-compatible IMDb data files from the Join Order Benchmark CSV dump.

This creates TWO outputs by default:
  - imdb.txt        : small teaching/demo dataset
  - imdb_large.txt  : larger dataset for later course modules

Both outputs use the same simplified v47 schema:
  - kind_type
  - title
  - company_type
  - company_name
  - movie_companies

The script uses movie_info_idx.csv internally to prefer popular movies when
rating/vote metadata is detectable, but it does NOT emit movie_info_idx or
movie_info rows. This keeps the early-course schema small while still allowing a
larger later-course database.

Output format expected by the v47 loader:
  table_name|field1|field2|...
"""

from __future__ import annotations

import argparse
import csv
import heapq
import re
import shutil
import sys
import tarfile
import urllib.request
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Set, Tuple


DEFAULT_URL = "https://event.cwi.nl/da/job/imdb.tgz"

NEEDED_FILES = {
    "title.csv",
    "kind_type.csv",
    "info_type.csv",
    "movie_info_idx.csv",
    "company_name.csv",
    "company_type.csv",
    "movie_companies.csv",
}

TITLE = {
    "id": 0,
    "title": 1,
    "kind_id": 3,
    "production_year": 4,
}

KIND_TYPE = {
    "id": 0,
    "kind": 1,
}

INFO_TYPE = {
    "id": 0,
    "info": 1,
}

MOVIE_INFO_IDX = {
    "id": 0,
    "movie_id": 1,
    "info_type_id": 2,
    "info": 3,
}

COMPANY_NAME = {
    "id": 0,
    "name": 1,
    "country_code": 2,
}

COMPANY_TYPE = {
    "id": 0,
    "kind": 1,
}

MOVIE_COMPANIES = {
    "id": 0,
    "movie_id": 1,
    "company_id": 2,
    "company_type_id": 3,
    "note": 4,
}


def log(message: str) -> None:
    print(message, file=sys.stderr)


def clean_string(value: str) -> str:
    if value is None:
        return "unknown"

    value = value.strip()
    if value == "" or value == r"\N":
        return "unknown"

    # v47 string deserialization uses operator>>, so avoid whitespace and pipes.
    value = value.replace("|", "_")
    value = re.sub(r"\s+", "_", value)
    value = re.sub(r"[^A-Za-z0-9_\-.,:\[\]\(\)&]+", "_", value)
    value = re.sub(r"_+", "_", value).strip("_")
    return value or "unknown"


def parse_int(value: str) -> Optional[int]:
    value = value.strip()
    if value == "" or value == r"\N":
        return None
    try:
        return int(value)
    except ValueError:
        return None


def parse_number(value: str) -> Optional[float]:
    if value is None:
        return None

    value = value.strip()
    if value == "" or value == r"\N":
        return None

    value = value.replace(",", "")
    match = re.search(r"-?\d+(?:\.\d+)?", value)
    if not match:
        return None

    try:
        return float(match.group(0))
    except ValueError:
        return None


def download_if_needed(url: str, tgz_path: Path) -> None:
    if tgz_path.exists() and tgz_path.stat().st_size > 0:
        log(f"Using existing archive: {tgz_path}")
        return

    log(f"Downloading {url} -> {tgz_path}")
    with urllib.request.urlopen(url) as response, tgz_path.open("wb") as out:
        shutil.copyfileobj(response, out)


def safe_member_name(member_name: str) -> str:
    return Path(member_name).name


def extract_needed_files(tgz_path: Path, csv_dir: Path) -> None:
    csv_dir.mkdir(parents=True, exist_ok=True)

    missing = [name for name in NEEDED_FILES if not (csv_dir / name).exists()]
    if not missing:
        log(f"Using existing extracted CSV files in {csv_dir}")
        return

    log(f"Extracting needed CSV files from {tgz_path}")
    found: Set[str] = set()

    with tarfile.open(tgz_path, "r:gz") as tar:
        for member in tar.getmembers():
            name = safe_member_name(member.name)
            if name not in NEEDED_FILES or not member.isfile():
                continue

            source = tar.extractfile(member)
            if source is None:
                continue

            dest = csv_dir / name
            with dest.open("wb") as out:
                shutil.copyfileobj(source, out)
            found.add(name)
            log(f"  extracted {name}")

    still_missing = sorted(
        NEEDED_FILES
        - found
        - {name for name in NEEDED_FILES if (csv_dir / name).exists()}
    )
    if still_missing:
        raise RuntimeError(f"Missing expected CSV files in archive: {still_missing}")


def read_csv_rows(path: Path) -> Iterable[List[str]]:
    with path.open("r", encoding="utf-8", newline="", errors="replace") as f:
        reader = csv.reader(f, delimiter=",", quotechar='"', escapechar="\\")
        for row in reader:
            if row:
                yield row


def load_id_name_table(path: Path, id_col: int, name_col: int) -> Dict[int, str]:
    result: Dict[int, str] = {}
    for row in read_csv_rows(path):
        if len(row) <= max(id_col, name_col):
            continue

        row_id = parse_int(row[id_col])
        if row_id is None:
            continue

        result[row_id] = row[name_col]

    return result


def find_movie_kind_ids(kind_type: Dict[int, str]) -> Set[int]:
    movie_ids = {
        kind_id
        for kind_id, kind in kind_type.items()
        if clean_string(kind).lower() == "movie"
    }

    if movie_ids:
        return movie_ids

    log("Warning: could not find kind_type 'movie'; using all title kinds.")
    return set(kind_type.keys())


def load_movie_ids_with_companies(movie_companies_csv: Path) -> Set[int]:
    movie_ids: Set[int] = set()

    for row in read_csv_rows(movie_companies_csv):
        if len(row) <= MOVIE_COMPANIES["movie_id"]:
            continue

        movie_id = parse_int(row[MOVIE_COMPANIES["movie_id"]])
        if movie_id is not None:
            movie_ids.add(movie_id)

    return movie_ids


def detect_info_type_ids(info_type: Dict[int, str]) -> Tuple[Set[int], Set[int], Set[int]]:
    """
    Return (vote_info_type_ids, rating_info_type_ids, rank_info_type_ids).
    The JOB/IMDbPy labels can vary, so this is deliberately fuzzy.
    """
    vote_ids: Set[int] = set()
    rating_ids: Set[int] = set()
    rank_ids: Set[int] = set()

    for info_type_id, raw_name in info_type.items():
        name = clean_string(raw_name).lower()

        if "vote" in name or "votes" in name or "number_of_votes" in name:
            vote_ids.add(info_type_id)

        if "rating" in name and "mpaa" not in name and "certificate" not in name:
            rating_ids.add(info_type_id)

        if "top_250" in name or "top250" in name or "rank" in name:
            rank_ids.add(info_type_id)

    return vote_ids, rating_ids, rank_ids


def load_popularity_metrics(
    movie_info_idx_csv: Path,
    info_type: Dict[int, str],
) -> Dict[int, Tuple[int, float, int]]:
    """
    Return movie_id -> (votes, rating, top_250_bonus).

    If vote/rating columns are not detected, many movies will simply get zeros
    and the script falls back to recent-year selection.
    """
    vote_ids, rating_ids, rank_ids = detect_info_type_ids(info_type)

    if not vote_ids:
        log("Warning: could not detect a votes-like info_type in movie_info_idx.")
    if not rating_ids:
        log("Warning: could not detect a rating-like info_type in movie_info_idx.")

    votes_by_movie: Dict[int, int] = {}
    rating_by_movie: Dict[int, float] = {}
    rank_bonus_by_movie: Dict[int, int] = {}

    for row in read_csv_rows(movie_info_idx_csv):
        if len(row) <= max(MOVIE_INFO_IDX.values()):
            continue

        movie_id = parse_int(row[MOVIE_INFO_IDX["movie_id"]])
        info_type_id = parse_int(row[MOVIE_INFO_IDX["info_type_id"]])
        value = parse_number(row[MOVIE_INFO_IDX["info"]])

        if movie_id is None or info_type_id is None or value is None:
            continue

        if info_type_id in vote_ids:
            votes_by_movie[movie_id] = max(votes_by_movie.get(movie_id, 0), int(value))

        elif info_type_id in rating_ids:
            rating_by_movie[movie_id] = max(rating_by_movie.get(movie_id, 0.0), float(value))

        elif info_type_id in rank_ids:
            rank = int(value)
            if rank > 0:
                rank_bonus_by_movie[movie_id] = max(
                    rank_bonus_by_movie.get(movie_id, 0),
                    1000 - rank,
                )

    metrics: Dict[int, Tuple[int, float, int]] = {}
    all_movie_ids = set(votes_by_movie) | set(rating_by_movie) | set(rank_bonus_by_movie)

    for movie_id in all_movie_ids:
        metrics[movie_id] = (
            votes_by_movie.get(movie_id, 0),
            rating_by_movie.get(movie_id, 0.0),
            rank_bonus_by_movie.get(movie_id, 0),
        )

    return metrics


def select_titles(
    title_csv: Path,
    movie_kind_ids: Set[int],
    movie_ids_with_companies: Set[int],
    popularity_metrics: Dict[int, Tuple[int, float, int]],
    limit: int,
    min_votes: int,
    sort: str,
) -> List[List[str]]:
    """
    Select top N titles.

    sort="popular":
      votes DESC, rating DESC, top_250_bonus DESC, production_year DESC, title_id DESC

    sort="recent":
      production_year DESC, title_id DESC
    """
    if sort not in {"popular", "recent"}:
        raise ValueError("sort must be 'popular' or 'recent'")

    heap: List[Tuple[Tuple[float, ...], List[str]]] = []

    for row in read_csv_rows(title_csv):
        if len(row) <= max(TITLE.values()):
            continue

        title_id = parse_int(row[TITLE["id"]])
        kind_id = parse_int(row[TITLE["kind_id"]])
        year = parse_int(row[TITLE["production_year"]])

        if title_id is None or kind_id is None or year is None:
            continue
        if kind_id not in movie_kind_ids:
            continue
        if title_id not in movie_ids_with_companies:
            continue

        votes, rating, rank_bonus = popularity_metrics.get(title_id, (0, 0.0, 0))

        if sort == "popular":
            if votes < min_votes:
                continue
            key: Tuple[float, ...] = (float(votes), float(rating), float(rank_bonus), float(year), float(title_id))
        else:
            key = (float(year), float(title_id))

        item = (key, row)
        if len(heap) < limit:
            heapq.heappush(heap, item)
        elif key > heap[0][0]:
            heapq.heapreplace(heap, item)

    selected = [item[1] for item in heap]

    if sort == "popular":
        selected.sort(
            key=lambda row: (
                popularity_metrics.get(parse_int(row[TITLE["id"]]) or -1, (0, 0.0, 0))[0],
                popularity_metrics.get(parse_int(row[TITLE["id"]]) or -1, (0, 0.0, 0))[1],
                popularity_metrics.get(parse_int(row[TITLE["id"]]) or -1, (0, 0.0, 0))[2],
                parse_int(row[TITLE["production_year"]]) or -1,
                parse_int(row[TITLE["id"]]) or -1,
            ),
            reverse=True,
        )
    else:
        selected.sort(
            key=lambda row: (
                parse_int(row[TITLE["production_year"]]) or -1,
                parse_int(row[TITLE["id"]]) or -1,
            ),
            reverse=True,
        )

    return selected


def filter_movie_companies(
    path: Path,
    selected_movie_ids: Set[int],
    max_companies_per_movie: int,
) -> List[List[str]]:
    rows: List[List[str]] = []
    counts: Dict[int, int] = {}

    for row in read_csv_rows(path):
        if len(row) <= max(MOVIE_COMPANIES.values()):
            continue

        movie_id = parse_int(row[MOVIE_COMPANIES["movie_id"]])
        if movie_id is None or movie_id not in selected_movie_ids:
            continue

        current = counts.get(movie_id, 0)
        if max_companies_per_movie >= 0 and current >= max_companies_per_movie:
            continue

        rows.append(row)
        counts[movie_id] = current + 1

    return rows


def load_company_name_rows(path: Path, needed_company_ids: Set[int]) -> List[List[str]]:
    rows: List[List[str]] = []

    for row in read_csv_rows(path):
        if len(row) <= max(COMPANY_NAME.values()):
            continue

        company_id = parse_int(row[COMPANY_NAME["id"]])
        if company_id is not None and company_id in needed_company_ids:
            rows.append(row)

    return rows


def emit_imdb_txt(
    output: Path,
    selected_titles: Sequence[List[str]],
    kind_type: Dict[int, str],
    company_type: Dict[int, str],
    movie_company_rows: Sequence[List[str]],
    company_name_rows: Sequence[List[str]],
    sort_description: str,
) -> None:
    used_kind_ids = {
        parse_int(row[TITLE["kind_id"]])
        for row in selected_titles
        if len(row) > TITLE["kind_id"]
    }
    used_kind_ids = {kind_id for kind_id in used_kind_ids if kind_id is not None}

    used_company_type_ids = {
        parse_int(row[MOVIE_COMPANIES["company_type_id"]])
        for row in movie_company_rows
        if len(row) > MOVIE_COMPANIES["company_type_id"]
    }
    used_company_type_ids = {
        company_type_id
        for company_type_id in used_company_type_ids
        if company_type_id is not None
    }

    with output.open("w", encoding="utf-8") as out:
        out.write("# Generated from Join Order Benchmark IMDb CSV data.\n")
        out.write("# Simplified v47 schema: no movie_info or movie_info_idx output tables.\n")
        out.write("# Format: table_name|field1|field2|...\n")
        out.write(f"# Selection: {sort_description}\n\n")

        for kind_id in sorted(used_kind_ids):
            out.write(f"kind_type|{kind_id}|{clean_string(kind_type.get(kind_id, 'unknown'))}\n")
        out.write("\n")

        for company_type_id in sorted(used_company_type_ids):
            out.write(
                f"company_type|{company_type_id}|"
                f"{clean_string(company_type.get(company_type_id, 'unknown'))}\n"
            )
        out.write("\n")

        for row in company_name_rows:
            out.write(
                f"company_name|{row[COMPANY_NAME['id']]}|"
                f"{clean_string(row[COMPANY_NAME['name']])}|"
                f"{clean_string(row[COMPANY_NAME['country_code']])}\n"
            )
        out.write("\n")

        for row in selected_titles:
            out.write(
                f"title|{row[TITLE['id']]}|"
                f"{clean_string(row[TITLE['title']])}|"
                f"{row[TITLE['kind_id']]}|"
                f"{row[TITLE['production_year']]}\n"
            )
        out.write("\n")

        for row in movie_company_rows:
            note = "unknown"
            if len(row) > MOVIE_COMPANIES["note"]:
                note = clean_string(row[MOVIE_COMPANIES["note"]])

            out.write(
                f"movie_companies|{row[MOVIE_COMPANIES['id']]}|"
                f"{row[MOVIE_COMPANIES['movie_id']]}|"
                f"{row[MOVIE_COMPANIES['company_id']]}|"
                f"{row[MOVIE_COMPANIES['company_type_id']]}|"
                f"{note}\n"
            )


def build_output(
    output: Path,
    title_csv: Path,
    movie_companies_csv: Path,
    company_name_csv: Path,
    kind_type: Dict[int, str],
    company_type: Dict[int, str],
    movie_kind_ids: Set[int],
    movie_ids_with_companies: Set[int],
    popularity_metrics: Dict[int, Tuple[int, float, int]],
    limit: int,
    min_votes: int,
    max_companies_per_movie: int,
    sort: str,
    label: str,
) -> None:
    selected_titles = select_titles(
        title_csv,
        movie_kind_ids,
        movie_ids_with_companies,
        popularity_metrics,
        limit,
        min_votes,
        sort,
    )

    if sort == "popular" and not selected_titles:
        log(f"Warning: no popular titles selected for {label}; falling back to recent selection.")
        selected_titles = select_titles(
            title_csv,
            movie_kind_ids,
            movie_ids_with_companies,
            popularity_metrics,
            limit,
            0,
            "recent",
        )

    if not selected_titles:
        raise RuntimeError(f"No titles selected for {label} output.")

    selected_movie_ids = {
        parse_int(row[TITLE["id"]])
        for row in selected_titles
    }
    selected_movie_ids = {movie_id for movie_id in selected_movie_ids if movie_id is not None}

    movie_company_rows = filter_movie_companies(
        movie_companies_csv,
        selected_movie_ids,
        max_companies_per_movie,
    )

    needed_company_ids = {
        parse_int(row[MOVIE_COMPANIES["company_id"]])
        for row in movie_company_rows
        if len(row) > MOVIE_COMPANIES["company_id"]
    }
    needed_company_ids = {company_id for company_id in needed_company_ids if company_id is not None}

    company_name_rows = load_company_name_rows(company_name_csv, needed_company_ids)

    sort_description = (
        "popular movies using movie_info_idx internally"
        if sort == "popular"
        else "recent movies by production_year DESC"
    )

    emit_imdb_txt(
        output,
        selected_titles,
        kind_type,
        company_type,
        movie_company_rows,
        company_name_rows,
        sort_description,
    )

    log(f"[{label}] wrote {len(selected_titles)} titles to {output}")
    log(f"[{label}] included {len(company_name_rows)} company_name rows")
    log(f"[{label}] included {len(movie_company_rows)} movie_companies rows")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default=DEFAULT_URL)
    parser.add_argument("--workdir", type=Path, default=Path("job_imdb_data"))
    parser.add_argument("--archive", type=Path, default=None)

    parser.add_argument("--mode", choices=["small", "large", "both"], default="both")

    parser.add_argument("--small-output", type=Path, default=Path("imdb.txt"))
    parser.add_argument("--small-limit", type=int, default=25)
    parser.add_argument("--small-min-votes", type=int, default=1)
    parser.add_argument("--small-max-companies-per-movie", type=int, default=4)

    parser.add_argument("--large-output", type=Path, default=Path("imdb_large.txt"))
    parser.add_argument("--large-limit", type=int, default=10000)
    parser.add_argument("--large-min-votes", type=int, default=0)
    parser.add_argument("--large-max-companies-per-movie", type=int, default=8,
                        help="Use -1 to keep all movie_companies rows for each selected movie.")

    parser.add_argument("--sort", choices=["popular", "recent"], default="popular")

    args = parser.parse_args()

    args.workdir.mkdir(parents=True, exist_ok=True)
    archive = args.archive or (args.workdir / "imdb.tgz")
    csv_dir = args.workdir / "csv"

    download_if_needed(args.url, archive)
    extract_needed_files(archive, csv_dir)

    kind_type = load_id_name_table(csv_dir / "kind_type.csv", KIND_TYPE["id"], KIND_TYPE["kind"])
    info_type = load_id_name_table(csv_dir / "info_type.csv", INFO_TYPE["id"], INFO_TYPE["info"])
    company_type = load_id_name_table(csv_dir / "company_type.csv", COMPANY_TYPE["id"], COMPANY_TYPE["kind"])

    movie_kind_ids = find_movie_kind_ids(kind_type)
    movie_ids_with_companies = load_movie_ids_with_companies(csv_dir / "movie_companies.csv")
    popularity_metrics = load_popularity_metrics(csv_dir / "movie_info_idx.csv", info_type)

    if args.mode in {"small", "both"}:
        build_output(
            args.small_output,
            csv_dir / "title.csv",
            csv_dir / "movie_companies.csv",
            csv_dir / "company_name.csv",
            kind_type,
            company_type,
            movie_kind_ids,
            movie_ids_with_companies,
            popularity_metrics,
            args.small_limit,
            args.small_min_votes,
            args.small_max_companies_per_movie,
            args.sort,
            "small",
        )

    if args.mode in {"large", "both"}:
        build_output(
            args.large_output,
            csv_dir / "title.csv",
            csv_dir / "movie_companies.csv",
            csv_dir / "company_name.csv",
            kind_type,
            company_type,
            movie_kind_ids,
            movie_ids_with_companies,
            popularity_metrics,
            args.large_limit,
            args.large_min_votes,
            args.large_max_companies_per_movie,
            args.sort,
            "large",
        )


if __name__ == "__main__":
    main()
