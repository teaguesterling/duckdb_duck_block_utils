DuckDB Extension Template
=========================

**Status:** Production Ready \| **Version:** 2.0.0 \| **License:** MIT

A **comprehensive** template for building [DuckDB](https://duckdb.org) extensions with *modern C++ practices* and full CI/CD support.

> **Important:** This template requires DuckDB v1.0+ and a C++17 compatible compiler. See [Requirements](#requirements) for details.

Features
--------

-   **Modern Build System**
    -   CMake-based configuration
    -   Supports `vcpkg` and `conan`
    -   Cross-platform compatibility
-   **Testing Infrastructure**
    -   Unit tests with GoogleTest
    -   Integration tests with sqllogictest
    -   Coverage reports via `lcov`
-   **CI/CD Pipeline**
    -   GitHub Actions workflows
    -   Automatic releases
    -   Multi-platform builds (Linux, macOS, Windows)

Installation
------------

Install directly from the DuckDB CLI:

``` {.sql}
INSTALL extension_name FROM community;
LOAD extension_name;
```

Or build from source:

``` {.bash}
git clone https://github.com/user/extension.git
cd extension
make release
make test
```

Quick Start
-----------

1.  **Clone** the repository
2.  **Configure** your extension name in `CMakeLists.txt`
3.  **Implement** your functions in `src/`
4.  **Test** with `make test`
5.  **Release** via GitHub Actions

API Reference
-------------

### Scalar Functions

The `my_function` scalar function accepts a `VARCHAR` and returns a `BIGINT`:

``` {.sql}
-- Basic usage
SELECT my_function('input') AS result;

-- With table data
SELECT id, my_function(name) AS processed
FROM my_table
WHERE active = true;
```

### Table Functions

Use `my_table_function` to generate rows:

``` {.sql}
SELECT * FROM my_table_function(10, 'config.json');
```

Configuration
-------------

Configure via DuckDB settings:

``` {.sql}
SET extension_option = 'value';
SET extension_debug = true;
```

Contributing
------------

We welcome contributions! Please read our [Contributing Guide](CONTRIBUTING.md) before submitting PRs.

> **Note for contributors:** All PRs must include tests and pass CI checks. Run `make format` before committing.

------------------------------------------------------------------------

[Report Issues](https://github.com/user/extension/issues) \| [Discord](https://discord.gg/duckdb) \| [DuckDB Docs](https://duckdb.org/docs)

Made with `<3` by the DuckDB Community
