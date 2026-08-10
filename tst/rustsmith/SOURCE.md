# RustSmith fixed corpus

Source: <https://github.com/rustsmith/rustsmith>

Revision: commit `dcf049c66f094779ab9d08e9caf6e3c61e2c6cae`.

The corpus contains the first 100 candidate seeds whose reference executable
finishes within ten seconds.  The exact accepted seed list is in `cases.tsv`.
Every program is stored separately with its command-line arguments and the
stdout produced by the official Rust 1.90.0 compiler.  The build graph runs
them in fixed shards of ten to avoid one graph node per large generated source.
The reference compiler reports commit
`1159e78c4747b02ef996e55082b704c09b970588`.

The pinned checkout no longer resolves `com.andreapivetta.kolor:kolor:1.0.0`
from the defunct JCenter repository.  To build the generator, remove that
dependency and replace its API with no-op color helpers.  Color is used only
by the debug logger, whose `DEBUG` constant is false.

Refresh from a temporary checkout and an official Rust 1.90.0 toolchain with:

```sh
git -C /tmp/rustsmith apply /path/to/trustme/tst/rustsmith/generator-kolor.patch
nix shell nixpkgs#jdk17 --command \
  bash -lc 'cd /tmp/rustsmith && ./gradlew --no-daemon build'
nix shell nixpkgs#jdk17 --command \
  tst/rustsmith/import.py /tmp/rustsmith /tmp/rust-1.90/bin/rustc
```
