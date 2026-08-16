# cargo

A Cargo-compatible package manager and build driver for the trustme
toolchain.

Supported commands:

```text
cargo build [Cargo options]
cargo test [Cargo options] [-- test arguments]
cargo vendor [PATH] --manifest-path Cargo.toml
```

`build` and `test` support Cargo manifests and workspaces, path and vendored
registry dependencies, semver requirements, patches, renamed/optional/target
dependencies, feature unification, editions and Cargo target kinds. Builds use
the Cargo build-script protocol, host/target separation, incremental timestamps,
parallel jobs, proc macros, and trustme's deferred C-codegen mode.

Backend-only inputs use Cargo's unstable option namespace instead of replacing
standard Cargo flags:

```text
-Zvendor-dir=DIR
-Zlib-search=DIR
-Zemit-mmir
-Zdry-run
-Zpause
```

`vendor` follows Cargo's destination and `--manifest-path` interface. The test
graph additionally uses `-Zarchive=FILE.tar.zst` to create a reproducible,
checksum-verified archive for transport between isolated build nodes.
