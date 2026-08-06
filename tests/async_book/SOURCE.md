# Asynchronous Programming in Rust code-fence corpus

Source: <https://github.com/rust-lang/async-book>

Revision: commit `43891cedf954e991657ba97c2e3ba743d97827ce`.

The importer applies the shared Rust documentation-fence adapter and verifies
each program with the official Rust 1.90 compiler.  Four of the 91 Rust fences
are standalone, dependency-free runtime programs.  Most other fences use the
external `futures` ecosystem or mdBook includes from Cargo examples, so they
are not rewritten into misleading single-file cases.  The four retained files
run in one build node.

Refresh from a temporary checkout and an official Rust 1.90 toolchain with:

```sh
tests/async_book/import.py /tmp/async-book /tmp/rust-1.90/bin/rustc
```
