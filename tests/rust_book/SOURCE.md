# The Rust Programming Language listing corpus

Source: <https://github.com/rust-lang/book>

Revision: commit `917544888a55e4da7109bdba8c88c893c0da70f4`.

The importer considers every binary target under `listings/` and every library
target containing unit tests.  It copies only the crate's Rust sources into an
empty directory, compiles the target with the official Rust 1.90 compiler, and
runs it.  The 288 retained targets therefore need neither Cargo dependencies
nor non-source fixtures and exit successfully within two seconds.  Each
listing remains a separate directory under `upstream/`; build nodes run at
most ten targets.

Refresh from a temporary checkout and an official Rust 1.90 toolchain with:

```sh
tests/rust_book/import.py /tmp/book /tmp/rust-1.90/bin/rustc
```
