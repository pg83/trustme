# Rust 1.90 run-pass corpus

Source: <https://github.com/rust-lang/rust/tree/1159e78c4747b02ef996e55082b704c09b970588/tests/ui>

Revision: tag `1.90.0` (tag object
`d8401009a052a5efaed3f5c901c76dd733c04fbe`), commit
`1159e78c4747b02ef996e55082b704c09b970588`.

`upstream/` contains 2,933 self-contained `run-pass` tests selected by
`import.py`. Feature-gated tests are preserved: a failure in an unsupported
language feature is part of the compatibility signal, not a reason to omit
the test. Per-test compile and runtime flags are preserved too. The files
retain their paths relative to `tests/ui`. Target conditions are evaluated for
the build's native `x86_64-unknown-linux-gnu` test environment. The build never
downloads or unpacks an upstream archive to run these tests.

Refresh from a temporary checkout with:

```sh
tests/rust_1_90/import.py /tmp/rust-1.90.0
```
