# Full-gate failure triage and fix priorities

This file contains unfinished work only. Priorities are ordered by the number
of independently reproduced failures that a shared fix can plausibly remove.
Source locations are routing signatures, not proof of a shared root cause.

## Current baseline

The complete fast gate was run on 2026-08-22 at commit `1c8482622` in the
clang Nix environment on all 78 available cores:

```text
nix --extra-experimental-features 'nix-command flakes' develop .#clang -c \
  ./build -B .build-clang -j 78 -k test
```

The `test` group is the complete fast semantic gate. `resvg` is not a
dependency of that group: it lives in the separate `slow_tests` group and
was not run or counted here. The gate log contains no `resvg` invocation.

Before the gate, and again before reclassification, the current CAS dependency
roots were built:

```text
nix --extra-experimental-features 'nix-command flakes' develop .#clang -c \
  ./build -B .build-clang -j 78 -k \
  rustc cargo libstd rust_test_helpers rust_lib_dependencies
```

Artifacts:

- full log:
  `.build-clang/full-gate-20260822-no-resvg.log`;
- independent reruns:
  `.build-clang/reclass-20260822-full-no-resvg/results.jsonl`;
- classified records:
  `.build-clang/classification-20260822-full-no-resvg/records.jsonl`;
- signature clusters:
  `.build-clang/classification-20260822-full-no-resvg/clusters.json`.

The graph contained 15,139 nodes. The full run completed 15,090 and reported
49 broken targets. All 49 target commands were rerun independently against
the published roots. Forty-eight failures reproduced; RustSmith seed 36
completed in isolation and is the only load-sensitive result. The subsequent
ThinBox, async-drop, coroutine-storage, async-argument, trait-object,
projection-bound, specialization, `IntoFuture`, and empty-array coercion point
fixes, followed by the declarative-macro hygiene fix, closed twenty-eight
independently rerun nodes; preserving the generic context while evaluating an
associated const pattern closed one more, leaving 19.

| result | nodes |
|---|---:|
| complete fast-gate graph | 15,139 |
| green in the full parallel run | 15,090 |
| failed in the full parallel run | 49 |
| reproduced immediately after the full gate | 48 |
| passed in isolation | 1 |
| fixed by subsequent point reruns | 29 |
| still failing independently | 19 |

Manual inspection normalised two mechanical classifier labels:

- `coroutine/issue-93161.rs` exits 248 because the compiler takes
  `SIGFPE`; the crashing stack reaches layout from MIR constant propagation;
- `async-drop/async-drop-initial.rs` compiles successfully, then its program
  takes `SIGABRT` while polling generated async-drop glue, so it is a runtime
  failure rather than a compiler abort.

The resulting current population is:

| current result | nodes |
|---|---:|
| compiler BUG, MIR error, signal, or generated C++ failure | 1 |
| wrong runtime behaviour, panic, abort, or output | 10 |
| stable timeout | 8 |
| **total independently reproduced** | **19** |

There are no carried failures from an older sweep. This full run supersedes
the previous 631-node baseline and the later “12 current + 25 carried”
accounting.

## P1: remaining compiler-internal failures

After the P0 routing above, one compiler failure remains:

| signature or route | nodes | cases |
|---|---:|---|
| compiler `SIGFPE` in layout reached from MIR const propagation | 1 | `coroutine/issue-93161.rs` |

## P1: runtime semantics

Ten programs compile but execute incorrectly:

| family | nodes | cases |
|---|---:|---|
| lifetime-erased `TypeId` / `Any` distinctions | 3 | `type-id-higher-rank`, `any-lifetime-escape-higher-rank`, doctest `core/src/any.rs:660` |
| anonymous-scope `type_name` paths | 2 | `issues/issue-61894.rs`, coretest `any::dyn_type_name` |
| RustSmith stdout mismatch | 2 | seeds 19 and 102 |
| drop elaboration after a raw-pointer write | 1 | `drop/issue-90752-raw-ptr-shenanigans.rs` |
| generic-assert captured diagnostic text | 1 | `feature-gate-generic_assert.rs` |
| adjacent stack allocation layout | 1 | Miri `adjacent-allocs.rs` |

The three `TypeId`/`Any` cases depend on distinguishing higher-ranked and
`'static` function/trait-object types. trustme currently erases lifetimes
from HIR path parameters, so this family needs a representation change rather
than three point fixes.

The two type-name cases both expose unnamed `#N` scopes, but expected output
differs by context: `issue-61894` needs the containing inherent method in the
path, while `dyn_type_name` expects the anonymous marker itself. Minimise
both before changing all anonymous path naming.

## P2: stable timeouts

Eight nodes hit their timeout again in isolation:

| limit | case |
|---:|---|
| 60 s | UI `impl-trait/recursive-type-alias-impl-trait-declaration-too-subtle-2.rs` |
| 60 s | UI `associated-type-bounds/trait-params.rs` |
| 60 s | Exercism `palindrome-products` |
| 60 s | UI `next-solver/normalize/normalize-allow-too-many-vars.rs` |
| 60 s | UI `consts/large-zst-array-77062.rs` |
| 60 s | UI `coroutine/issue-87142.rs` |
| 10 min | RustSmith seed 7 |
| 60 s | `mir/mir_heavy_promoted.rs` |

Classify each timeout by its last progressing compiler phase before attempting
a fix. Recursive alias resolution, solver explosion, large generated MIR, and
slow generated-program compilation are not interchangeable root causes.

## Load-sensitive result

RustSmith seed 36 exceeded its 10-minute node limit during the 78-way full
run, then compiled and passed in isolation in roughly eight minutes. It is not
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
`*self` needs `Self: Copy`, which the bounds a derived `Clone` adds do not
give, and emitting it breaks the standard library.

Refresh this baseline only with a complete `test` run followed by
`dev/gate_reclassify.py` and `dev/gate_classify.py`. Keep `resvg` and the
other `slow_tests` explicitly outside the fast-gate counts.
