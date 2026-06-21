# BuzzDB

<div align="center">
  <img src="https://github.com/user-attachments/assets/04ebaaac-3ce6-46dd-8e37-535ecd1808f8" alt="BuzzDB logo" width="340">
</div>

<p align="center">
  <strong>An educational relational database system written in C++.</strong>
</p>

<p align="center">
  <a href="https://buzzdb-docs.readthedocs.io/">Documentation</a>
  ·
  <a href="https://buzzdb-docs.readthedocs.io/part1/setup.html">Setup Guide</a>
  ·
  <a href="LICENSE">License</a>
</p>

BuzzDB is a modular database system designed for teaching and experimentation. It introduces core database internals step by step, including storage management, indexing, query execution, transactions, recovery, concurrency control, and query optimization.

The repository is organized as a sequence of standalone C++ versions, so each file can highlight one concept without hiding the implementation behind a large framework.

## Getting Started

BuzzDB only requires a C++17-capable compiler.

For environment setup, see:

- [BuzzDB Linux setup guide](https://buzzdb-docs.readthedocs.io/part1/setup.html)
- [C++ setup on macOS](https://medium.com/hayoung-techlog/setup-for-c-on-mac-d2056a025c85)

## Build

Compile any standalone BuzzDB version with:

```bash
g++ -std=c++17 -O3 -Wall -Werror -Wextra <module_name>.cpp
```

For example:

```bash
g++ -std=c++17 -O3 -Wall -Werror -Wextra 104-buzzdb.cpp
./a.out
```

To include debugging symbols:

```bash
g++ -std=c++17 -O3 -Wall -Werror -Wextra -g <module_name>.cpp
```

On some Apple silicon setups, you may need to include `<cassert>` if your compiler reports:

```text
error: use of undeclared identifier 'assert'
```

## Core Components

| Area | What It Covers |
| --- | --- |
| Data types | `Field` values for `INT`, `FLOAT`, and `STRING` data. |
| Tuples | `Tuple` storage, serialization, and row-level representation. |
| Pages | `SlottedPage` layout for fixed-size page storage. |
| Buffer manager | In-memory page caching with LRU-style replacement. |
| Storage manager | Loading, flushing, and persisting pages on disk. |
| Indexing | Hash indexes, B+tree range access, and related index experiments. |
| Query execution | Operators such as scan, select, projection, aggregation, join, and sort. |
| Transactions | Transaction boundaries, updates, aborts, and recovery examples. |
| Recovery | Shadow paging, WAL, ARIES-style logging, page LSNs, checkpoints, and media recovery. |
| Concurrency control | 2PL, deadlocks, isolation levels, timestamp ordering, OCC, MVCC, and SSI. |
| Query optimization | Statistics, cardinality estimation, join ordering, Cascades-style memo search, LIP, and UDF inlining. |

## Additional Projects

BuzzDB also includes smaller database-adjacent experiments:

- **Patricia trie** for compact lookup structures.
- **Learned indexes** using regression and neural models.
- **Columnar storage and compression** with SIMD-friendly layouts.
- **SIMD operations** for high-performance data processing experiments.
- **Spatial and vector indexes** such as R-tree, ND-tree, and HNSW examples.

## Repository Layout

- `NN-buzzdb.cpp`: self-contained teaching versions of BuzzDB.
- `z-*.cpp`: focused experiments and side projects.
- `job_imdb_data/`, `imdb*.txt`: datasets used by query execution and optimization examples.
- `refs/`: reference implementations and papers used while building optimizer versions.
- `booking.txt`: toy seat-booking workload for transactions and concurrency control.

## Contributing

Please open an issue or pull request if you find a bug, have a teaching improvement, or want to extend one of the modules.

## License

BuzzDB is licensed under the [Apache-2.0 License](LICENSE).
