#!/usr/bin/env python3
"""Create a tiny one-flight booking dataset for BuzzDB recovery examples."""

from __future__ import annotations

import argparse
from pathlib import Path


def write_booking_data(output: Path) -> None:
    rows = [
        "# Tiny booking dataset for WAL/recovery and concurrency-control examples.",
        "# Format: table_name|field1|field2|...",
        "",
        "flights|1|BZ101|SFO|JFK|2026-07-01",
        "",
        "seats|1|1|1A|available|none",
        "seats|2|1|1B|available|none",
        "seats|3|1|2A|available|none",
        "seats|4|1|2B|available|none",
        "",
        "# holds starts empty; v52 inserts a couple of active holds as statements.",
    ]
    output.write_text("\n".join(rows) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("booking.txt"))
    args = parser.parse_args()

    write_booking_data(args.output)
    print(f"wrote {args.output}")


if __name__ == "__main__":
    main()
