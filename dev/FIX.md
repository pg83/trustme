# Full-gate failure triage and fix priorities

This file contains unfinished work only.

Snapshot: 2026-08-14, commit `22c0951eeb16`. The full gate was run from the
clang Nix environment with all 78 available cores:

```text
./build -B .build-clang -j 78 -k test
```

Full log: `/tmp/trustme-full-gate-20260814-new.log`.

All failed nodes were then rerun independently with the same compiler and the
published `libstd` artifact. The independent rerun data is in
`/tmp/trustme-reclass-20260814-new`; the classified records are in
`/tmp/trustme-classification-20260814-new`.

| result | tests |
|---|---:|
| total active fast-gate nodes | 14,149 |
| passed in the full gate | 13,109 |
| failed in the full gate | 1,040 |
| failures reproduced independently | 1,039 |
| parallel-only flakes | 1 |

| class | tests |
|---|---:|
| compiler BUG, MIR TODO/ERROR, assertion, exception, or signal | 139 |
| accepted Rust rejected by parser, resolver, type checker, or driver | 646 |
| generated C++/assembly/link failure | 39 |
| wrong runtime result, panic, abort, or generated-code assertion | 132 |
| missing rejection/diagnostic | 74 |
| stable timeout | 9 |
| parallel-only flake | 1 |

The two tables above are the immutable full-gate baseline. The unfinished
counts below incorporate focused reruns; another full gate remains forbidden
until this file is exhausted.

## Highest-impact work order

These are semantic areas. A shared diagnostic location is not sufficient proof
of a shared root cause; split a row whenever minimal reproducers diverge.

1. Trait lookup, normalization, and inference: 11 missing-method failures, 27
   missing-impl failures, 14 unresolved inference variables, 10 inferred trait
   obligations left ambiguous, and 9 const-value relation failures. Total
   adjacent impact: 71 tests, but the 27 missing impls include a large
   `TransmuteFrom` subgroup and must not be treated as one solver bug without
   comparing their goals.
2. Macro and attribute expansion: 24 unresolved macro invocations and 13
   unsupported derive applications. The largest concrete macro families are
   `pattern_type` (6), `deref` (5), `iter` (5), and `concat_bytes` (3); the
   derive failures contain 10 `CoercePointee` and 2 `UnsizedConstParamTy`
   cases.
3. Opaque types (`TAIT`, `RPIT`, `RPITIT`, and async return types): 22
   opaque-bearing result-type relation failures at
   `hir_typeck_expr_cs.cpp:2196` and 13 erased types rejected outside return
   position at `hir_conv_main_bindings.cpp:455`. Total adjacent impact: 35
   tests. All 22 relation failures were rerun after the opaque identity and
   refined-RPITIT fixes and still reproduce, so this row contains a different
   root cause.
4. Result typing and coercion outside opaque types: 20 heterogeneous failures
   share the final diagnostic at `hir_typeck_expr_cs.cpp:2196`. They include
   block/loop results, match ergonomics, function-item coercions, patterns,
   async types, and ordinary generic inference; the common error line is not a
   common implementation root.
5. Unresolved types reaching translation: 11 infer/async/closure types rejected
   by mangling at `trans_mangling.cpp:257`, plus 4 `sizeof` operations on infer
   types at `trans_target.cpp:496`.
6. Shared backend/runtime families: 12 pointer equality/provenance tests, 9
   `core::num::dec2flt` library tests, 9 Miri x86-intrinsic aborts, 9 async-drop
   output mismatches, and 5 remaining `track_caller` cases spanning a trait
   object, closure, FFI, indexing, and macro declaration.

## Internal compiler failures

There are 141 unfinished compiler-internal failures in 91 stable signatures.

| signature or root cause | tests | note |
|---|---:|---|
| non-encodable type, `trans_mangling.cpp:257` | 11 | infer variables, async and closure types reach mangling |
| inline-assembly token assertion, `parse_token.h:71` | 5 | five Rust Reference asm fragments |
| intrinsic/unsized translation errors, `mir_helpers.h:108` | 5 | two `vtable_align`; one each `fmuladdf32`, `fadd_fast`, and extern-type alignment |
| `sizeof` on infer type, `trans_target.cpp:496` | 4 | const-generic/type-relation cases |
| 87 smaller stable signatures | 116 | one to three tests each |

The eight signal failures are two SIGILLs at `mir_operations.cpp:1473`, two
SIGSEGVs at `hir_conv_main_bindings.cpp:1430`, the recursive pattern-lowering
SIGSEGV at `mir_from_hir.cpp:4760`, and three other one-test SIGSEGV signatures.
Four uncaught exceptions and five explicit MIR TODOs are included in the 141.

## Accepted Rust rejected by the front end

The 490 unfinished ordinary compiler rejections and one pathless-`--extern`
driver rejection split as follows:

| area | tests | largest stable groups |
|---|---:|---|
| parser | 168 | 89 at `parse_parseerror.cpp:63`, 57 at line 56, 19 at line 68, 3 in `parse_common.cpp` |
| type checking, HIR lowering, and resolution | 247 | result relation 42 (22 opaque-bearing, 20 other), missing method 11, missing impl 27 |
| macro and attribute expansion | 69 | unknown macro 24, missing derive 13 |
| MIR/CTFE rejection | 6 | 4 in constant evaluation, 2 in MIR lowering |
| command-line driver | 1 | pathless `--extern` |

All are positive tests already accepted by Rust 1.90. A normal mrustc error is
therefore a compiler deficiency, not an expected diagnostic.

## Missing diagnostics

Seventy-four negative tests compile successfully: 61 Rust Reference cases and
13 Nomicon cases. The largest source areas are:

- Rust Reference: diagnostics attributes (9), inline assembly (7), destructors
  (7), name resolution (7), and trait items (4);
- Nomicon: drop checking (3), subtyping (3), and lifetime mismatch (2).

Source chapters are only routing information. Before implementing a diagnostic,
group the concrete examples by the missing language rule.

## Generated code and linking

Thirty-three tests produce C++ rejected by clang. Repeated generated-code
families are incomplete or unknown generated types (8), invalid `main` ABI
(4), invalid assignments (4), enum-discriminant narrowing (3), pointer to
small-integer casts (3), and inline assembly (3). Eight smaller failures remain.

Six tests reach the linker: three miss generated constant symbols, two refer to
intentional native symbols, and one exercises native-link directives.

## Runtime semantics

- 106 executables panic with exit 101. The panic exit itself is not a root
  cause; the repeated semantic families are listed in the impact order above.
- Eleven outputs differ: nine async-drop tests and RustSmith seeds 19 and 102.
- Eleven executables abort: nine Miri x86-intrinsic cases, one async Miri
  double-free, and one library test.
- Five generated executables hit assertions: two SysV empty-struct ABI cases,
  three SIMD/library intrinsic cases (`reduce_add`, `round`, and `neg`).

## Performance and flakes

Nine nodes time out in isolated reruns:

- Exercism `palindrome-products`;
- `deriving/issue-58319.rs`;
- `consts/large-zst-array-77062.rs`;
- `consts/const-eval/enum_discr.rs`;
- `mir/mir_heavy_promoted.rs`;
- `impl-trait/recursive-impl-trait-type-direct.rs`;
- `impl-trait/recursive-type-alias-impl-trait-declaration-too-subtle-2.rs`;
- `for-loop-while/label_break_value.rs`;
- RustSmith seed 7.

RustSmith seed 36 is the sole parallel-only flake: it timed out in the full gate
and passed in the independent rerun.

## Fix discipline

For every compiler fix:

1. Add a minimal `tst/unit/test_*.rs` that is green with the exact Rust 1.90
   compiler before changing mrustc.
2. Confirm that unit is red on current mrustc.
3. Fix the shared compiler path, not the corpus expectation.
4. Run only the new unit, the affected original triggers, and the unit group.
5. Remove completed rows from this file instead of commenting them out.

Do not run another full gate until this file is exhausted. Reclassification is
performed by `dev/gate_reclassify.py` and `dev/gate_classify.py`.
