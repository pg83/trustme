# The Rustonomicon code-fence corpus

Source: <https://github.com/rust-lang/nomicon>

Revision: commit `5012a37c682b26c4e19433888ed2ca9b129696ca`.

The importer uses the same Markdown and rustdoc conventions as the Rust
Reference importer, then verifies every fence with the official Rust 1.90
compiler.  The 110 self-contained fences comprise 88 runtime-pass programs,
three compile-only programs, and 19 intentional compile-fail programs.  Files
remain separate under `upstream/`; build nodes run at most ten fences.

Refresh from a temporary checkout and an official Rust 1.90 toolchain with:

```sh
tests/nomicon/import.py /tmp/nomicon /tmp/rust-1.90/bin/rustc
```
