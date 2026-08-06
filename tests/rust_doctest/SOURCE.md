# Rust 1.90 library doctest corpus

Source: <https://github.com/rust-lang/rust/tree/1159e78c4747b02ef996e55082b704c09b970588/library>

Revision: tag `1.90.0` (tag object
`d8401009a052a5efaed3f5c901c76dd733c04fbe`), commit
`1159e78c4747b02ef996e55082b704c09b970588`.

Every file under `upstream/` is one runnable Rust code fence extracted
from the `core`, `alloc`, or `std` documentation.  The importer applies
rustdoc's hidden-line convention and adds a standalone `main` when needed.
Compile-only, ignored, non-Rust, and `no_run` blocks are not runtime cases.
`cases.tsv` records the exact upstream source and line for every program.

Refresh from a temporary checkout with:

```sh
tests/rust_doctest/import.py /tmp/rust-1.90.0
```
