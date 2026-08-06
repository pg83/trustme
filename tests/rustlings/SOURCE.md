# Rustlings solved-exercise corpus

Source: <https://github.com/rust-lang/rustlings>

Revision: commit `3b7ca447f100b6542713532e64ea7e758aeaa6b0`
(Rustlings 6.5.0, MSRV 1.88).

`upstream/` contains the 94 official files from `solutions/`, one per exercise.
`cases.tsv` records the execution mode from `rustlings-macros/info.toml`: normal
exercises are compiled and run as binaries, while test exercises are compiled
with `rustc --test` and their generated harness is run.  Build nodes contain at
most ten exercises.

Refresh from a temporary checkout with:

```sh
tests/rustlings/import.py /tmp/rustlings
```
