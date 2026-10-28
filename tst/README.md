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

- **`std_src`** — download `rustc-1.90.0-src`, apply the fixed source and
  manifest adjustments in `tst/std/fetch.py`, and add the `trustme-stdlib`
  shim. Output: `rust-src.tar`. (Set `RUST_SRC` to reuse a local tree.)
- **`libstd`** — build the standard library and `lib/proc_macro` from that source
  with `cargo`. Output: `libstd.tar`. Depends on `rustc`, `cargo`, `std_src`.
  **Shared by every project test.**
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

Three unusually long upstream library tests are kept with the real-project
integration tests instead of the fast semantic gate: the exhaustive f32 and
f64 `flt2dec` equivalence tests and `slice::select_nth_unstable`. Run them and
resvg together as `./build slow_tests`.

## running

```
./build libstd            # just the shared standard library
./build unit              # every one-file compiler regression
./build perf              # compile-time performance regressions
./build resvg             # the whole resvg chain (build + test)
./build test              # the fast semantic corpus
./build slow_tests        # resvg + unusually long upstream library tests
```

These are heavy and only run when asked for, never as part of the default
`./build`. If the environment lacks system CA certs, set
`SSL_CERT_FILE=/etc/ssl/cert.pem` for the vendor step.

## compiler coverage

CLI units that emit compiler output without a runnable binary use
`//@ compile-only`. They can verify that output with repeated
`//@ output-contains: .mir expected text` directives: the suffix names a file
next to the requested compiler output, and the remaining text must occur in it.
The suffix can also name a stage dump, such as `_1_ast.rs`, `_2_hir.rs`, or
`_3_mir.rs`. These checks also work for runnable and `//@ crate-type: lib` units.

```
./build -Dcoverage=1 coverage
```

The separate `rustc_coverage` and `rustc_debug_coverage` binaries are instrumented
and built without ThinLTO. The debug variant enables the production compiler's
`TRUSTME_DEBUG` traces. CLI regressions check both the Rust program's behaviour
and the trace's parser/codegen stage boundaries. Every test node has its own
profile output. `coverage_release` and `coverage_debug` export each compiler's
own profiles under `.build/coverage/release/` and `.build/coverage/debug/`.
`coverage` unions their line records and writes LCOV and a per-file summary
under `.build/coverage/`. Each source line counts once, including lines present
only in one variant. Function and branch records remain in the separate
reports: combining their different control-flow mappings in LLVM can lose data.
The ordinary report includes only the local CLI regressions; external test
corpora are not included in the coverage graph.
Only production sources in `bin/rustc/` count; tests, dependencies and generated
sources are excluded. The C++ unit runner and `_ut.cpp` tests are excluded from
the coverage graph: only paths reached by compiler invocations count.

Line coverage, the LCOV totals and `summary.txt` all count the exported `DA`
records. The coverage executable saves its counters before deliberate
`abort()` calls as well, so rejected input and fault diagnostics are measured.

To select the next Rust regression, rank completely uncovered braced `if`,
`else`, and `case` bodies by their number of instrumented lines:

```
python3 dev/coverage_branches.py .build/coverage/coverage.info --limit 30
```

Nested bodies count only once when their enclosing branch is wholly uncovered.
The condition and boundary lines are excluded because LCOV can attribute them
to the preceding branch. Use `--source-revision COMMIT` when the report belongs
to an earlier source revision, and `--json PATH` to save the complete ranking.

### CLI fault tests

`WireBoard::chaos` holds the compiler's pool-allocated fault interface.
Ordinary `rustc` uses an idle implementation; `rustc_chaos` and `rustc_coverage`
can read deterministic
`TRUSTME_CHAOS=name@skip,...` scripts. Each rule passes `skip` calls, fails the
next one once, and prints `CHAOS name`. The script implementation lives in
`tst/unit/` and does not count as production coverage.

An ordinary Rust unit can request a fault:

```rust
//@ chaos: backend-spawn@0
//@ compile-fail: system returned -1
//@ crate-type: lib
pub fn answer() -> u32 { 42 }
```

Run it with `./build unit_chaos_backend_spawn`, or add `-Dcoverage=1` to collect
its profile. Auxiliary crates are built without the fault; the runner arms it
only for the main compiler invocation and requires both the fault marker and
the expected diagnostic. System-rustc mode excludes these compiler-specific
tests. Extra fixtures are declared with `//@ input: name`; compile flags can
use `$DIR` for the directory containing the test.
