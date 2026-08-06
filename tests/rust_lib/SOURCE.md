# Rust 1.90 library unit-test corpus

Source: <https://github.com/rust-lang/rust/tree/1159e78c4747b02ef996e55082b704c09b970588/library>

Revision: tag `1.90.0` (tag object
`d8401009a052a5efaed3f5c901c76dd733c04fbe`), commit
`1159e78c4747b02ef996e55082b704c09b970588`.

`upstream/` contains the individual source files from `coretests/tests`,
`alloctests/tests`, `alloctests/testing`, and `std/tests`.  Explicit `#[test]`
functions are listed separately in `cases.tsv`.  Tests from one top-level
module share a compiled harness, while every function remains its own runtime
build node selected by its exact libtest name.

Refresh from a temporary checkout with:

```sh
tests/rust_lib/import.py /tmp/rust-1.90.0
```
