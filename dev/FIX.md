# Full-gate failure triage and fix priorities

Snapshot: 2026-08-12, commit 4bb1e6a5c, full gate `./build -j 24 -k test`
(~14 500 tests). 999 failing cases, all verified pre-existing relative to the
lifetime-erasure refactor (the identical gate on pre-erasure 526848ea8 fails
the same set; the erasure itself cost 12 tests, all fixed/re-baselined).

Classification method: per-case rerun of every failing test with the failure
signature (assert/BUG location, first error, signal) clustered by root cause.

## P0 — miscompiles: accepted code runs WRONG (~23)

The compiler silently produces incorrect programs. Highest severity.

| cluster | tests | notes |
|---|---|---|
| rust-quiz stdout | 1 | wrong semantics: #020 break-with-value in condition |
| rust_lib runtime panics | 18 | std `error.rs` tests; partly blocked on missing backtrace support (borders P2 feature work) |
| slice doctest | 1 | `core/src/slice/mod.rs` |

## P1 — compiler crashes on valid code (~178)

One cluster = one bug.

| location | tests | example |
|---|---|---|
| ~12 smaller clusters (2-4 each) | ~35 | mangling:98, expr_cs:1909 (TAIT placeholder), hir_hir:482, mir_from_hir:1866/1596/7386, typeck_common:653/397, expand:5452/1997, trans_main:575, mir_operations:640 |
| unclassified aborts (miri/gccrs/rustlings/doctest categories, not re-run individually) | 81 | |

## P2 — missing language features (~475)

Parser gaps (237):
- lifetime syntax: raw `'r#a`, label forms (26)
- `..`-related syntax in new positions (14)
- fn delegation `reuse` (11)
- `become` tail calls (7)
- `gen` blocks, `pattern_type!` (5), inline-asm fragments (5), specialization
  `default` items (5), C-string literals `c".."`/`cr".."`, misc (~160 spread
  over many single-test syntaxes)

Typeck incompleteness (238):
- "Failed to find an impl" family (25+)
- "type annotations needed" / "Failed to infer" (23)
- "Method X doesn't match trait" — the type-alias-impl-trait epic (17)
- assorted type mismatches, receiver-type limitations, erased-type
  restrictions ("Use of an erased type outside of a function return")

## P3 — cheap wins or debatable (~80)

- missing diagnostics (80): compile-fail tests for lints/attribute checks the
  fork never implemented; implement or re-baseline.

## X — environment flakes (27)

Pass when re-run in isolation; fail only under the parallel gate load
(timeouts etc.).

## Recommended order (by ROI)

1. Quiz miscompiles (deep dive: control-flow lowering)
2. Remaining classified crash clusters, largest-cluster first
3. Unclassified aborts, clustered by root cause before fixing
4. Parser features, largest-cluster first
5. Typeck/TAIT epic (largest, hardest)

Raw data: per-case signatures and per-bucket test lists were produced by the
triage scripts in the session scratchpad (`fail-rows2.json`,
`priorities.json`); regenerate by re-mining a full-gate log if needed.
