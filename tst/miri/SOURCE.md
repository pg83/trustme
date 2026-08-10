# Miri positive native-pass corpus

Source: <https://github.com/rust-lang/miri>

The source tree is `e89c03a1fa5282b1dd3954062b5eee52e7ed1987`, exactly
the `src/tools/miri` tree stored by `rust-lang/rust` tag `1.90.0` (commit
`1159e78c4747b02ef996e55082b704c09b970588`).  In the Miri repository that
tree is materialized by commit `2042e98bd9b41a6cf2063c47da92f9cec8ee591e`.

The importer considers every file under `tests/pass`.  Each file is copied
alone into an empty directory and retained only if the official Rust
1.90.0 compiler can build it and the native executable exits successfully.
This excludes nightly-only, Miri-shim-only, non-native, and auxiliary-file
cases without rewriting upstream sources.  `cases.tsv` is the exact retained
set.  Build nodes run fixed shards of ten tests.

Refresh from a temporary checkout and an official Rust 1.90.0 toolchain with:

```sh
git clone https://github.com/rust-lang/miri.git /tmp/miri
git -C /tmp/miri worktree add --detach /tmp/miri-rust-1.90 \
  2042e98bd9b41a6cf2063c47da92f9cec8ee591e
tst/miri/import.py /tmp/miri-rust-1.90 /tmp/rust-1.90/bin/rustc
```
