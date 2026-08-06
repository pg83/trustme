# Rust By Example runtime corpus

Source: <https://github.com/rust-lang/rust-by-example>

Revision: commit `15308f3e951814ef3475d2b58f48276e6b17b9af`.

The importer extracts individual Rust code fences from the 204 Markdown source
chapters, adds a standalone `main` where rustdoc would do so, and checks each
program with the official Rust 1.90 compiler.  A fence is retained only when it
compiles with its documented edition and exits successfully by itself within
two seconds.  The resulting 211 programs remain individual files under
`upstream/`; build nodes run at most ten programs each.

Refresh from a temporary checkout and an official Rust 1.90 toolchain with:

```sh
tests/rust_by_example/import.py /tmp/rust-by-example /tmp/rust-1.90/bin/rustc
```
