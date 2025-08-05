# BuzzDB: An Educational Database System

<br>

<div align="center">
  <img src="https://github.com/user-attachments/assets/04a4f1fd-a6f9-42f0-8845-1aafae64081a" alt="buzzdb" width="40%">
</div>  

<br>

BuzzDB is a relational database system written in C++ with a modular design for educational purposes. 

It consists of advanced storage management, indexing, query execution components.
  
## Getting Started

To compile BuzzDB, ensure your environment is set up for C++ development. Follow the environment setup guides below for Mac and Linux:

- **Mac Setup Guide**: [C++ on Mac](https://medium.com/hayoung-techlog/setup-for-c-on-mac-d2056a025c85)
- **Linux Setup Guide**: [BuzzDB Docs - Setup](https://buzzdb-docs.readthedocs.io/part1/setup.html)

## Modules

BuzzDB includes the following core components:

- **Data Types and Fields**: Defines a `Field` class that can store different data types (`INT`, `FLOAT`, `STRING`), allowing flexible use in various database operations.
- **Tuple Management**: Implements a `Tuple` class that aggregates multiple `Field` instances, representing rows in a database table with support for serialization.
- **Page and Slot Management**: Provides a `SlottedPage` class for fixed-size page storage, using slots to efficiently manage data within pages.
- **Buffer Management**: Offers a `BufferManager` that maintains in-memory pages, applying an LRU replacement policy to manage a cache of pages.
- **Storage Manager**: Manages on-disk storage by loading and flushing pages to a persistent storage file.
- **Hash Index**: Implements a simple hash-based indexing mechanism for efficient data access.
- **Multi-threaded Hash Index**: Parallelized hash index implementation for high-throughput environments
- **Range Queries**: B+tree-based index for range-based data retrieval
- **Query Operators**: Includes operators like `ScanOperator`, `SelectOperator`, and `HashAggregationOperator` for query execution.

BuzzDB also includes the following additional projects:
  
- **Patricia Trie**: Optimized trie for fast lookups and memory-efficient storage.
- **Learned Indexes**: Neural network and regression models for predictive indexing
- **Columnar Storage & Compression**: Efficient data storage with SIMD-based columnar compression
- **SIMD Operations**: High-performance operations using SIMD for complex queries

## Compilation Instructions

To compile BuzzDB, use the following commands:

- **Compilation**: Compile a particular version of BuzzDB as follows:

```bash
  g++ -std=c++14 -O3 -Wall -Werror -Wextra <module_name>.cpp
```
- **Compilation with Debugging Symbols**: Use -g flag to enable debugging with gdb or lldb:

```
g++ -std=c++14 -O3 -Wall -Werror -Wextra -g <module_name>.cpp
```

## Contributions

We welcome contributions to enhance BuzzDB’s performance and extend its capabilities. Feel free to submit pull requests, report issues, and join discussions.

## License

BuzzDB is licensed under the [Apache-2.0 License](LICENSE).

## About

BuzzDB is intended for use in educational and research contexts. Contributions from database enthusiasts and students are welcome.
