# cargo

A minimal, lockfile-driven cargo replacement for the rustc (mrustc-derived)
toolchain, written in Go.

It does **not** resolve dependencies — it trusts a committed `Cargo.lock` — and
currently implements one stage:

```
cargo vendor --manifest-dir DIR --out OUT.tar.zst
```

`vendor` reads `DIR/Cargo.lock`, downloads every registry dependency from the
crates.io CDN, verifies each `.crate` against its lockfile SHA-256, lays the
extracted sources out exactly as `cargo vendor` would (bare crate name, or
`name-version` when a crate appears at multiple versions), writes the
`.cargo/config.toml` vendor redirect, and packs the whole tree into a
reproducible `.tar.zst`.

The archive is the hermetic input a downstream build consumes offline — see
`tests/`, where the vendor step is one graph node and the offline build is
another.

Set `SSL_CERT_FILE` to a CA bundle if the environment lacks system certs.
