# tests

Each test is **one real-world Rust project, built by our toolchain and then
exercised**. Every stage is a build-graph node, so work is cached and
parallelised like any other build, and the expensive standard-library build is
**shared** across all projects rather than repeated per test.

```
   std_src ─► libstd ─────────────────────────┐
  (fetch +   (build libcore/liballoc/libstd/   │  (shared: built once)
   patch      libtest/libproc_macro once,      │
   rust src)  → libstd.tar)                     ▼
                              cargo (Go)   ┌─► resvg  ──► (render test)
                                  │        │  (build offline against
   resvg_src ─► resvg_vendor ─────┘────────┘   libstd, then run its test)
  (clone @rev)  (vendor deps → tar.zst)
```

The nodes:

- **`std_src`** — download `rustc-1.90.0-src`, apply `std/rustc-1.90.0-src.patch`,
  add the `mrustc-stdlib` shim. Output: `rust-src.tar`. (Set `RUST_SRC` to reuse
  a local tree.)
- **`libstd`** — build the standard library and `libproc_macro` from that source
  with `minicargo`, using `std/script-overrides` and the manifest overrides.
  Output: `libstd.tar`. Depends on `rustc`, `minicargo`, `std_src`. **Shared by
  every project test.**
- **`<proj>_src`** — clone the project at a pinned revision → `*-src.tar`.
  (Set `SRC_OVERRIDE` to reuse a local checkout.)
- **`<proj>_vendor`** — `cargo vendor` the project's locked dependencies into a
  hermetic `tar.zst`. Depends on the Go `cargo`.
- **`<proj>`** — the plan's node 2: unpack the vendor tar and `libstd.tar`, build
  the project offline with `minicargo`, and run its test (for resvg: render a
  reference SVG and validate the PNG). Depends on `<proj>_vendor`, `libstd`,
  `<proj>_src`, `rustc`, `minicargo`.

Nodes exchange **tar archives** because the build engine only promotes declared
file outputs; each node unpacks what it needs into a private temp dir.

## running

```
./build libstd            # just the shared standard library
./build resvg             # the whole resvg chain (build + test)
./build test              # every project test
```

These are heavy and only run when asked for, never as part of the default
`./build`. If the environment lacks system CA certs, set
`SSL_CERT_FILE=/etc/ssl/cert.pem` for the vendor step.
