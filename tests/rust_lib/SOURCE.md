# Rust 1.90 library unit-test corpus

Source: <https://github.com/rust-lang/rust/tree/d8401009a052a5efaed3f5c901c76dd733c04fbe/library>

Revision: tag `1.90.0`, commit `d8401009a052a5efaed3f5c901c76dd733c04fbe`.

`upstream/` contains the individual source files from `coretests/tests`,
`alloctests/tests`, `alloctests/testing`, and `std/tests`.  Explicit `#[test]`
functions are listed separately in `cases.tsv`.  Tests from one top-level
module share a compiled harness, while every function remains its own runtime
build node selected by its exact libtest name.

Refresh from a temporary checkout with:

```sh
tests/rust_lib/import.py /tmp/rust-1.90.0
```
