# Rust 1.90 run-pass corpus

Source: <https://github.com/rust-lang/rust/tree/1.90.0/tests/ui>

Revision: tag `1.90.0`, commit `d8401009a052a5efaed3f5c901c76dd733c04fbe`.

`upstream/` contains the self-contained, assertion-bearing `run-pass` tests
selected by `import.py`. The files retain their paths relative to `tests/ui`.
The build never downloads or unpacks an upstream archive to run these tests.

Refresh from a temporary checkout with:

```sh
tests/rust_1_90/import.py /tmp/rust-1.90.0
```
