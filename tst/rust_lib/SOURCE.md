# Rust 1.90 library unit-test corpus

Source: <https://github.com/rust-lang/rust/tree/1159e78c4747b02ef996e55082b704c09b970588/library>

Revision: tag `1.90.0` (tag object
`d8401009a052a5efaed3f5c901c76dd733c04fbe`), commit
`1159e78c4747b02ef996e55082b704c09b970588`.

`upstream/` contains the individual source files from `coretests/tests`,
`alloctests/tests`, `alloctests/testing`, and `std/tests`. Every entry in
`cases.tsv` is one independently compiled and executed test node.

The importer first finds explicit source `#[test]` functions, then asks the
exact upstream rustc 1.90.0 to expand each real harness using the target mrustc
driver's backend capability cfgs. It retains a source item only when rustc
emitted a libtest descriptor with the same `source_file` and `start_line`.
Thus rustc evaluates the upstream cfg expressions; there is no second local
cfg expression evaluator. Explicit source tests omitted by the target harness
remain auditable in `excluded_cases.tsv`; they are not build nodes and are not
reported as passing tests.

Refresh from a temporary checkout with:

```sh
tst/rust_lib/import.py /tmp/rust-1.90.0 \
  --rustc /tmp/rustup/toolchains/1.90.0-x86_64-unknown-linux-gnu/bin/rustc \
  --target-rustc .build-clang/bin/rustc
```

The source and compiler are both verified against commit
`1159e78c4747b02ef996e55082b704c09b970588`. The Cargo dependency build and all
std harness expansions use every available CPU.
