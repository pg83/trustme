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

- **`std_src`** — download `rustc-1.90.0-src`, apply the patch from
  `bin/rustc/overrides`,
  add the `mrustc-stdlib` shim. Output: `rust-src.tar`. (Set `RUST_SRC` to reuse
  a local tree.)
- **`libstd`** — build the standard library and `lib/proc_macro` from that source
  with `cargo`. The manifest and build-script overrides are embedded into the
  Cargo binary by the root `build.py`; no override file is read at runtime.
  Output: `libstd.tar`. Depends on `rustc`, `cargo`, `std_src`. **Shared by
  every project test.**
- **`<proj>_src`** — clone the project at a pinned revision → `*-src.tar`.
  (Set `SRC_OVERRIDE` to reuse a local checkout.)
- **`<proj>_vendor`** — `cargo vendor` the project's locked dependencies into a
  hermetic `tar.zst`. Depends on the Go `cargo`.
- **`<proj>`** — the plan's node 2: unpack the vendor tar and `libstd.tar`, build
  the project offline with `cargo`, and run its test (for resvg: render a
  reference SVG and validate the PNG). Depends on `<proj>_vendor`, `libstd`,
  `<proj>_src`, `rustc`, `cargo`.

Nodes exchange **tar archives** because the build engine only promotes declared
file outputs; each node unpacks what it needs into a private temp dir.

## unit regressions

`tst/unit/test_*.rs` are self-contained one-file regressions — one per
compiler fix. Each is its own graph node (`unit_<name>`): compiled against the
shared `libstd` and run, and must exit 0. They depend only on `libstd` and
`rustc`, so they are cheap once the standard library is built.

Two fixes are not expressible as a single std-only file and are covered by the
resvg integration instead: forwarding a `#[repr(C)]` above a `#[derive]` to a
proc-macro derive (harfrust/bytemuck `Pod`), and Cargo's `src/main.rs`
binary-target discovery (resvg's CLI binary).

## performance regressions

`tst/perf/test_*.rs` contains self-contained programs whose compile time is
the regression. They use the same compiler-and-runner setup as unit tests, but
are excluded from the normal `unit`, `lite_tests`, and `test` groups. Run one as
`./build perf_<name>` or the complete performance corpus as `./build perf`.

## running

```
./build libstd            # just the shared standard library
./build unit              # every one-file compiler regression
./build perf              # compile-time performance regressions
./build resvg             # the whole resvg chain (build + test)
./build test              # everything under test (unit + projects)
```

These are heavy and only run when asked for, never as part of the default
`./build`. If the environment lacks system CA certs, set
`SSL_CERT_FILE=/etc/ssl/cert.pem` for the vendor step.
