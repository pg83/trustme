# tests

Each test is **one real-world Rust project, built by our toolchain and then
exercised**. A test is expressed as a small chain of build-graph nodes, so the
work is cached and parallelised like any other build:

```
                 cargo (Go)                 rustc + minicargo
                     │                             │
        ┌────────────┴───────────┐     ┌───────────┴────────────┐
        ▼                        ▼     ▼                        ▼
  <proj>_src ──► <proj>_vendor ──►  <proj>_build ──► <proj>_test
  (fetch source)  (vendor deps       (offline build of         (run the built
                   → tar.zst)         the project against       binary / its
                                      a from-source libstd)     test)
```

The nodes, in order:

1. **`std_src`** — download `rustc-1.90.0-src`, apply `std/rustc-1.90.0-src.patch`,
   and drop in the `mrustc-stdlib` shim crate. Output: `rust-src.tar`.
2. **`libstd`** — build libcore / liballoc / libstd / libtest (and friends) from
   that source with `minicargo`, using `std/script-overrides` and
   `std/rustc-1.90.0-overrides.toml`. Output: `libstd.tar`. Depends on `rustc`,
   `minicargo`, `std_src`.
3. **`<proj>_src`** — fetch the project's source at a pinned revision.
4. **`<proj>_vendor`** — `cargo vendor` the project's locked dependencies into a
   hermetic `tar.zst`. Depends on the Go `cargo`.
5. **`<proj>_build`** — unpack the vendor tar, and build the project offline with
   `minicargo` against the from-source libstd. Depends on `<proj>_vendor`,
   `libstd`, `rustc`, `minicargo`.
6. **`<proj>_test`** — run the project's own check (for resvg: render a reference
   SVG and validate the PNG). Grouped under `test`.

The graph lives in the repo-root `build.py`; this directory holds the per-test
data (`resvg/run.py`) and the shared libstd build inputs (`std/`).

## running

```
./build resvg_test        # the whole chain for resvg
./build test              # every project test
```

These are heavy (a from-scratch libstd plus a full project build); they only run
when asked for, never as part of the default `./build`.
