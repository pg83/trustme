# Rust 1.90 positive UI compile corpus

Source: <https://github.com/rust-lang/rust/tree/1159e78c4747b02ef996e55082b704c09b970588/tests/ui>

Revision: tag `1.90.0`, commit
`1159e78c4747b02ef996e55082b704c09b970588`.

The corpus contains 2,506 self-contained `check-pass` files and 368
`build-pass` files.  Compile flags and editions are retained in `cases.json`.
The current compiler has no dedicated check-only frontend mode, so check-pass
files are sent through its available compilation pipeline as libraries.  Any
late-stage failures remain visible test failures rather than a reason to omit
the upstream positive invariant.  Aux-crate, revision, target-specific, and
multi-file cases are reserved for adapters that can reproduce those inputs.
Build nodes contain at most ten source files.

Refresh from a temporary checkout with:

```sh
tests/rust_ui_compile/import.py /tmp/rust-1.90.0
```
