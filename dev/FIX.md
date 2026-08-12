# Full-gate failure triage and fix priorities

Snapshot: 2026-08-12, commit 4bb1e6a5c, full gate `./build -j 24 -k test`
(~14 500 tests). 999 failing cases, all verified pre-existing relative to the
lifetime-erasure refactor (the identical gate on pre-erasure 526848ea8 fails
the same set; the erasure itself cost 12 tests, all fixed/re-baselined).

Classification method: per-case rerun of every failing test with the failure
signature (assert/BUG location, first error, signal) clustered by root cause.

## P0 — miscompiles: accepted code runs WRONG (~49)

The compiler silently produces incorrect programs. Highest severity.

| cluster | tests | notes |
|---|---|---|
| f16 arithmetic | 24 | every `core/num/f16.rs` doctest aborts at runtime on `assert_eq`; single root in half-float codegen |
| rust-quiz stdout | 3 | wrong semantics: #033 method resolution `RangeFull` vs `FnOnce` (prints "4" instead of "24"), #001 macro statement counting, #020 break-with-value in condition |
| rust_lib runtime panics | 18 | std `error.rs` tests; partly blocked on missing backtrace support (borders P2 feature work) |
| slice doctest | 1 | `core/src/slice/mod.rs` |

## P1 — compiler crashes on valid code (~243)

One cluster = one bug. Fixing the top 8 clusters greens ~50 tests.

| location | tests | example |
|---|---|---|
| SEGV: unbounded `findImpl` bound recursion | 14 | `associated-types/normalize-cycle-in-eval.rs` (rustc #74868); needs a cycle guard |
| `macro_rules_macro_rules.cpp:2116` "No arm matched" | 13 | `attributes/dont-dup-expr-attrs.rs` |
| `trans_target.cpp:584` "sizeof on an erased type" | 7 | `impl-trait/in-ctfe/array-len.rs` |
| `mir_from_hir.cpp:4422` | 6 | `consts/const-expr-addr-operator.rs` |
| `trans_mangling.cpp:269` path-not-Generic | 5 | `const-generics/adt_const_params/116308.rs` |
| `hir_conv_constant_evaluation.cpp:681` | 5 | `consts/const-eval/issue-47971.rs` |
| `hir_typeck_helpers.cpp:5161` | 5 | `associated-types/issue-69398.rs` |
| `hir_typeck_expr_cs.cpp:8467` "Spare rules left" | 5 | `nll/unexpected-inference-var-ice-116599.rs` |
| `hir_typeck_resolve_common.cpp:160` | 5 | `const-generics/generic_const_exprs/issue-82268.rs` |
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

## P3 — cheap wins or debatable (~185)

- **`-Z` flag acceptance (105)**: `unpretty` (30), `validate-mir` (16),
  `mir-enable-passes` (9), `inline-mir` (6), `crate-attr` (6),
  `verbose-internals`, `print-type-sizes`, `lint-mir`, ... Most tests only
  need the flag to be *accepted*; making unknown `-Z` a no-op (or adding
  these as ignored) greens ~100 tests in a day. **Best ROI item overall.**
- missing diagnostics (80): compile-fail tests for lints/attribute checks the
  fork never implemented; implement or re-baseline.

## X — environment flakes (27)

Pass when re-run in isolation; fail only under the parallel gate load
(timeouts etc.).

## Recommended order (by ROI)

1. `-Z` no-op flag acceptance (~100 tests, trivial)
2. f16 codegen (24 tests, single root, restores runtime trust)
3. findImpl recursion guard for normalization cycles (14 tests)
4. Top-5 crash clusters (~36 tests)
5. Quiz miscompiles (deep dives: method resolution, macro hygiene)
6. Parser features, largest-cluster first
7. Typeck/TAIT epic (largest, hardest)

Raw data: per-case signatures and per-bucket test lists were produced by the
triage scripts in the session scratchpad (`fail-rows2.json`,
`priorities.json`); regenerate by re-mining a full-gate log if needed.
