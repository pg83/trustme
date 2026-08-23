# Full-gate failure triage and fix priorities

This file contains unfinished work only. Priorities are ordered by the number
of independently reproduced failures that a shared fix can plausibly remove.
Source locations are routing signatures, not proof of a shared root cause.

## Current baseline

The complete fast gate was run on 2026-08-23 at commit `12353394a` in the ix
clang environment on all 24 available cores:

```text
env -u CC -u CXX ../ix/ix run set/pg/libs -- bash -c \
  'export CC=cc CXX=c++ AR=llvm-ar SSL_CERT_FILE=/etc/ssl/cert.pem; \
   ./build -B .build-clang -j 24 -k test'
```

The `test` group is the complete fast semantic gate. `resvg` is not a
dependency of that group: it lives in the separate `slow_tests` group and was
not run or counted here.

Before the gate, the current CAS dependency roots were published explicitly:

```text
./build -B .build-clang -j 24 -k \
  rustc cargo libstd rust_test_helpers rust_lib_dependencies
```

Artifacts:

- full log: `.build-clang/full-gate-20260823.log`;
- independent reruns: `.build-clang/reclass-20260823-full/results.jsonl`;
- classified records:
  `.build-clang/classification-20260823-full/records.jsonl`;
- signature clusters:
  `.build-clang/classification-20260823-full/clusters.json`.

The graph contained 15,333 nodes. The full run completed 15,219 and reported
114 broken targets. All 114 target commands were rerun independently against
the published roots. One hundred and thirteen failures reproduced. RustSmith
seed 36 completed in isolation and remains the only load-sensitive result.

| result | nodes |
|---|---:|
| complete fast-gate graph | 15,333 |
| green in the full parallel run | 15,219 |
| failed in the full parallel run | 114 |
| reproduced immediately after the full gate | 113 |
| passed in isolation | 1 |
| fixed by subsequent point reruns | 96 |
| still failing independently | 17 |

The 96 trait-object `Debug` link failures were closed by canonicalising concrete
trait-impl value paths before translation enumeration and C symbol emission.
All 96 original commands passed against freshly published roots; the rerun is
recorded in
`.build-clang/reclass-20260823-link-fixed-ix-env/results.jsonl`. Four stable
timeouts are interactive programs blocked on inherited stdin, so they are
harness failures rather than compiler-progress failures.

The resulting current population is:

| current result | nodes |
|---|---:|
| wrong runtime behaviour, panic, abort, or output | 7 |
| compiler abort or wrong compiler diagnostic | 5 |
| stable timeout | 5 |
| **total independently reproduced** | **17** |

This full run supersedes the 2026-08-22 15,139-node baseline and all subsequent
point accounting.

## P1: runtime semantics

Seven programs compile but execute incorrectly:

| nodes | family | cases |
|---:|---|---|
| 2 | RustSmith stdout mismatch | seeds 19 and 102 |
| 2 | missing enum/aggregate drops | `drop/issue-23611-enum-swap-in-drop.rs`, `drop/issue-90752.rs` |
| 1 | discarded-expression drop | `issues/issue-6892.rs` |
| 1 | linear inlined stack allocation | `codegen/StackColoring-not-blowup-stack-issue-40883.rs` |
| 1 | stack overflow in threaded `OnceLock` list | `std/src/sync/once_lock.rs:53` |

## P2: compiler aborts and diagnostics

Five cases fail before producing a usable artifact:

| nodes | signature | cases |
|---:|---|---|
| 2 | `mir_from_hir.cpp:2405`, region-only trait-object cast mismatch | Miri `dyn-upcast.rs`, `multiple-supertraits-modulo-binder.rs` |
| 1 | const evaluator rejects a live `Vec` result | `consts/control-flow/drop-precise.rs` |
| 1 | MIR treats a function item address as a static | UI `issues/issue-56870.rs` |
| 1 | TAIT inherent lookup emits a type mismatch instead of the expected lookup failure | unit `tait_inherent_lookup_outside_defining_scope` |

## P3: stable timeouts

| nodes | limit | classification | cases |
|---:|---:|---|---|
| 3 | 60 s | harness leaves interactive stdin open | Rust Book guessing-game examples |
| 1 | 60 s | harness leaves interactive stdin open | doctest `std/src/io/mod.rs:2379` |
| 1 | 10 min | compiler progress requires phase classification | RustSmith seed 7 |

Classify seed 7 by its last progressing compiler phase before attempting a
fix. Recursive alias resolution, solver explosion, large generated MIR, and
slow generated-program compilation are not interchangeable root causes.

## Load-sensitive result

RustSmith seed 36 exceeded its 10-minute node limit during the 24-way full run,
then compiled and passed in isolation in roughly eight minutes. It is not
included in the current failure count. Keep it as a gate-capacity signal; do
not turn it into a compiler-correctness issue unless it becomes a stable
timeout.

## Fix discipline

For every compiler fix:

1. Add a minimal `tst/unit/test_*.rs` that is green with the exact Rust 1.90
   compiler before changing trustme.
2. Confirm that unit is red on current trustme.
3. Fix the shared compiler path, not the corpus expectation.
4. Run only the new unit, the affected original triggers, and the `unit`
   group.
5. Remove completed rows from this file instead of commenting them out.
6. Re-run every affected original node against freshly published CAS roots.

A generic type that derives `Copy` and `Clone` still clones field by field:
`*self` needs `Self: Copy`, which the bounds a derived `Clone` adds do not give,
and emitting it breaks the standard library.

Refresh this baseline only with a complete `test` run followed by
`dev/gate_reclassify.py` and `dev/gate_classify.py`. Do not run another complete
gate until every row above has been closed by point reruns and `unit`. Keep
`resvg` and the other `slow_tests` explicitly outside the fast-gate counts.
