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
the Cargo build-script protocol, host/target separation, content-addressed
caching, parallel jobs, proc macros, and a split Rust/C++/link artifact graph.
Task outputs are stored content-addressed under `target/cas/`, indexed by canonical
task manifests under `target/uid/`, and consumed from CAS paths without a
materialized dependency tree.

Backend-only inputs use Cargo's unstable option namespace instead of replacing
standard Cargo flags:

```text
-Zvendor-dir=DIR
-Zlib-search=DIR
-Zemit-mmir
-Zdry-run
-Zpublish-deps
-Zpause
```

`-Zpublish-deps` is for producing a reusable sysroot bundle: it materializes
the metadata and object endpoints of dependency libraries after the graph has
finished. It does not change how tools exchange artifacts inside the graph.

`vendor` follows Cargo's destination and `--manifest-path` interface. The test
graph additionally uses `-Zarchive=FILE.tar.zst` to create a reproducible,
checksum-verified archive for transport between isolated build nodes.
