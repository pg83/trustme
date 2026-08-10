# Exercism Rust practice corpus

Source: <https://github.com/exercism/rust>

Revision: commit `afe248a156c147bed54f08ed52d88e50d17b0e7b`.

For each practice exercise, the importer pairs the official `.meta/example.rs`
solution with its integration-test sources.  Exercises with Cargo dependencies
are left out.  The remaining pair is retained only when the official Rust 1.90
compiler can build the solution as a library, build every test harness against
it, and pass the harness with `--include-ignored`.  That last option is
intentional: Exercism marks later tests ignored only to unlock them gradually.

The retained 92 exercises contain 93 harness files and 1,467 individual
`#[test]` functions.  Each exercise remains separate under `upstream/`; build
nodes run at most ten exercises.

Refresh from a temporary checkout and an official Rust 1.90 toolchain with:

```sh
tst/exercism_rust/import.py /tmp/exercism-rust /tmp/rust-1.90/bin/rustc
```
