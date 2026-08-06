# Rust 1.90 run-pass corpus

Source: <https://github.com/rust-lang/rust/tree/1159e78c4747b02ef996e55082b704c09b970588/tests/ui>

Revision: tag `1.90.0` (tag object
`d8401009a052a5efaed3f5c901c76dd733c04fbe`), commit
`1159e78c4747b02ef996e55082b704c09b970588`.

`upstream/` contains the self-contained stable `run-pass` tests
selected by `import.py`. The files retain their paths relative to `tests/ui`.
The build never downloads or unpacks an upstream archive to run these tests.

Refresh from a temporary checkout with:

```sh
tests/rust_1_90/import.py /tmp/rust-1.90.0
```
