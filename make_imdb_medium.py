#!/usr/bin/env python3
"""
Create a medium-sized BuzzDB IMDb file from imdb_large.txt.

The output keeps the same simplified schema as imdb.txt/imdb_large.txt, but
selects only the first N title rows and the movie_companies rows that reference
those titles. Referenced company/type rows are kept so joins remain valid.
"""

from __future__ import annotations

import argparse
import sys
from collections import defaultdict
from pathlib import Path
from typing import DefaultDict, Dict, Iterable, List, Optional, Set, Tuple


Row = Tuple[str, List[str]]


def parse_data_rows(path: Path) -> Iterable[Row]:
    with path.open("r", encoding="utf-8", errors="replace") as input_file:
        for line in input_file:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            parts = line.split("|")
            if len(parts) < 2:
                continue
            yield parts[0], parts[1:]


def require_field(table: str, fields: List[str], index: int) -> str:
    if len(fields) <= index:
        raise RuntimeError(f"Malformed {table} row: {'|'.join(fields)}")
    return fields[index]


def select_titles(source: Path, max_titles: int) -> Tuple[List[List[str]], Set[str], Set[str]]:
    titles: List[List[str]] = []
    title_ids: Set[str] = set()
    kind_ids: Set[str] = set()

    for table, fields in parse_data_rows(source):
        if table != "title":
            continue
        if len(titles) >= max_titles:
            break

        title_id = require_field(table, fields, 0)
        kind_id = require_field(table, fields, 2)
        titles.append(fields)
        title_ids.add(title_id)
        kind_ids.add(kind_id)

    if not titles:
        raise RuntimeError(f"No title rows found in {source}")

    return titles, title_ids, kind_ids


def select_movie_companies(
    source: Path,
    title_ids: Set[str],
    max_companies_per_movie: int,
) -> Tuple[List[List[str]], Set[str], Set[str]]:
    movie_companies: List[List[str]] = []
    company_ids: Set[str] = set()
    company_type_ids: Set[str] = set()
    counts: DefaultDict[str, int] = defaultdict(int)

    for table, fields in parse_data_rows(source):
        if table != "movie_companies":
            continue

        movie_id = require_field(table, fields, 1)
        if movie_id not in title_ids:
            continue
        if max_companies_per_movie >= 0 and counts[movie_id] >= max_companies_per_movie:
            continue

        movie_companies.append(fields)
        counts[movie_id] += 1
        company_ids.add(require_field(table, fields, 2))
        company_type_ids.add(require_field(table, fields, 3))

    return movie_companies, company_ids, company_type_ids


def collect_lookup_rows(
    source: Path,
    kind_ids: Set[str],
    company_ids: Set[str],
    company_type_ids: Set[str],
) -> Dict[str, List[List[str]]]:
    rows: Dict[str, List[List[str]]] = {
        "kind_type": [],
        "company_type": [],
        "company_name": [],
    }

    for table, fields in parse_data_rows(source):
        if table == "kind_type" and require_field(table, fields, 0) in kind_ids:
            rows[table].append(fields)
        elif table == "company_type" and require_field(table, fields, 0) in company_type_ids:
            rows[table].append(fields)
        elif table == "company_name" and require_field(table, fields, 0) in company_ids:
            rows[table].append(fields)

    return rows


def write_rows(output: Path, table: str, rows: List[List[str]]) -> None:
    with output.open("a", encoding="utf-8") as out:
        for fields in rows:
            out.write(table + "|" + "|".join(fields) + "\n")
        out.write("\n")


def build_medium(source: Path,
                 output: Path,
                 max_titles: int,
                 max_companies_per_movie: int) -> None:
    titles, title_ids, kind_ids = select_titles(source, max_titles)
    movie_companies, company_ids, company_type_ids = select_movie_companies(
        source,
        title_ids,
        max_companies_per_movie,
    )
    lookup_rows = collect_lookup_rows(source, kind_ids, company_ids, company_type_ids)

    output.write_text(
        "# Generated from imdb_large.txt.\n"
        "# Simplified v47 schema: no movie_info or movie_info_idx output tables.\n"
        "# Format: table_name|field1|field2|...\n"
        f"# Selection: first {len(titles)} title rows, "
        f"max {max_companies_per_movie} movie_companies rows per title.\n\n",
        encoding="utf-8",
    )

    write_rows(output, "kind_type", lookup_rows["kind_type"])
    write_rows(output, "company_type", lookup_rows["company_type"])
    write_rows(output, "company_name", lookup_rows["company_name"])
    write_rows(output, "title", titles)
    write_rows(output, "movie_companies", movie_companies)

    print(f"wrote {output}", file=sys.stderr)
    print(f"  kind_type: {len(lookup_rows['kind_type'])}", file=sys.stderr)
    print(f"  company_type: {len(lookup_rows['company_type'])}", file=sys.stderr)
    print(f"  company_name: {len(lookup_rows['company_name'])}", file=sys.stderr)
    print(f"  title: {len(titles)}", file=sys.stderr)
    print(f"  movie_companies: {len(movie_companies)}", file=sys.stderr)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path("imdb_large.txt"))
    parser.add_argument("--output", type=Path, default=Path("imdb_medium.txt"))
    parser.add_argument("--max-titles", type=int, default=5000)
    parser.add_argument("--max-companies-per-movie", type=int, default=8,
                        help="Use -1 to keep all rows for selected titles.")
    args = parser.parse_args()

    build_medium(
        args.source,
        args.output,
        args.max_titles,
        args.max_companies_per_movie,
    )


if __name__ == "__main__":
    main()
