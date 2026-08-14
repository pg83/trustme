# Full-gate failure triage and fix priorities

This file contains unfinished work only.

Snapshot: 2026-08-14, after the CTFE function-pointer and `track_caller`
changes. The full gate was run in the clang Nix environment as:

```text
./build -B .build-clang -j 78 -k test
```

Full log: `/tmp/trustme-full-gate-20260814-final.log`.

| result | tests |
|---|---:|
| total | 14,553 |
| passed in the full gate | 13,504 |
| failed in the full gate | 1,049 |
| fixed and rerun after the gate | 9 |
| remaining observed failures | 1,040 |
| remaining failures reproduced independently | 1,039 |
| passes independently (`RustSmith` seed 36) | 1 |

The nine post-gate fixes were one shared regression in compiler-generated
global-allocator shims. All nine affected nodes, including both existing unit
tests, pass after the fix. Every other failed node was rerun independently with
the same compiler and published `libstd` artifact.

| class | remaining tests |
|---|---:|
| compiler BUG, MIR TODO/ERROR, assertion, exception, signal | 139 |
| accepted Rust rejected by parser, resolver, type checker, or driver | 646 |
| generated C++/assembly/link failure | 39 |
| wrong runtime result, panic, or abort | 132 |
| missing rejection/diagnostic | 74 |
| stable timeout | 9 |
| parallel-only flake | 1 |

## Highest-impact work order

The rows below are semantic areas, not merely shared reporting locations. Work
top-down inside each area, but split a row again when minimal reproducers show
independent causes.

1. Opaque types (`TAIT`, `RPIT`, `RPITIT`, async return types): 42 result-type
   mismatches at `hir_typeck_expr_cs.cpp:2196`, 32 trait-method signature
   mismatches at `hir_typeck_main_bindings.cpp:710`, and 13 erased types rejected
   outside a return position at `hir_conv_main_bindings.cpp:455`. These 87
   failures are the largest adjacent model gap.
2. Trait lookup and inference: 29 missing-method failures, 25 missing-impl
   failures, 14 unresolved inference variables, 10 unsatisfied inferred bounds,
   and 9 const-value relation failures. Do not treat each diagnostic text as a
   separate feature until the solver goals have been compared.
3. Parser gaps with repeated syntax: 32 async/closure function-body forms, 23
   `reuse` delegation forms, 15 return-type-notation `..` forms, 10
   lifetime-in-path forms, and 9 `become` forms. The generic parser source lines
   also contain a long tail and are not themselves root causes.
4. Macro and attribute expansion: 24 unresolved macro invocations and 13
   unsupported derives, principally `CoercePointee` and
   `UnsizedConstParamTy`.
5. Unresolved types reaching translation: 11 non-encodable infer/async/closure
   types at `trans_mangling.cpp:257` and 4 `sizeof` operations on infer types at
   `trans_target.cpp:496`.
6. Shared runtime/backend families: 12 pointer-equality/provenance tests, 9
   `core::num::dec2flt` tests, 9 Miri x86-intrinsic aborts, 9 async-drop output
   mismatches, and 6 remaining `track_caller` cases spanning trait objects,
   closures, FFI, indexing, and panic locations.

## Internal compiler failures

There are 139 internal failures in 89 stable source-location signatures.

| signature or root cause | tests | note |
|---|---:|---|
| non-encodable type, `trans_mangling.cpp:257` | 11 | infer variables, async and closure types reach mangling |
| inline-assembly token assertion, `parse_token.h:71` | 5 | five Rust Reference asm fragments |
| `sizeof` on infer type, `trans_target.cpp:496` | 4 | const-generic/type-relation cases |
| `vtable_align` intrinsic | 2 | both in library pointer tests |
| `fmuladdf32` intrinsic | 1 | |
| `fadd_fast` intrinsic | 1 | |
| extern-type alignment | 1 | |
| 85 smaller signatures | 114 | one to three tests each |

The signal failures are included above: two SIGILLs at
`mir_operations.cpp:1473`, two SIGSEGVs at
`hir_conv_main_bindings.cpp:1430`, and three one-test SIGSEGV signatures.

## Accepted Rust rejected by the front end

The 645 compiler rejections and one pathless-`--extern` driver rejection split
as follows:

| area | tests | largest stable signatures |
|---|---:|---|
| parser | 275 | 144 at `parse_parseerror.cpp:63`, 109 at line 56, 19 at line 68 |
| type checking, HIR lowering, and resolution | 295 | opaque mismatch 42, trait method mismatch 32, missing method 29, missing impl 25 |
| macro and attribute expansion | 69 | unknown macro 24, missing derive 13 |
| MIR/CTFE rejection | 6 | |
| command-line driver | 1 | pathless `--extern` |

These are positive tests already green under Rust 1.90. A normal mrustc error is
therefore a compiler deficiency, not an expected diagnostic.

## Diagnostics, generated code, and runtime semantics

- 74 negative tests compile successfully: 61 Rust Reference cases and 13
  Nomicon cases. Group them by the missing language rule before implementing a
  diagnostic.
- 33 tests produce C++ rejected by clang. The largest families are incomplete
  or unknown generated types (8), invalid `main` ABI (4), invalid assignments
  (4), enum-discriminant narrowing (3), pointer-to-small-integer casts (3), and
  inline assembly (3). Eight smaller failures remain.
- Six tests reach the linker: three miss generated constant symbols, two refer
  to intentional native symbols, and one link-directive case needs separate
  inspection.
- 106 executables panic with exit 101. The generic panic text is not a root
  cause; the repeated semantic families are listed in the impact order above.
- Eleven outputs differ: nine async-drop tests and `RustSmith` seeds 19 and
  102.
- Ten executables abort: nine Miri x86-intrinsic cases and one library test.
- Five additional runtime assertions are separate: two SysV empty-struct ABI
  cases and three unimplemented SIMD platform intrinsics (`reduce_add`,
  `round`, and `neg`).

## Performance and flakes

Nine nodes time out in isolated reruns:

- Exercism `palindrome-products`;
- `deriving/issue-58319.rs`;
- `consts/large-zst-array-77062.rs`;
- `consts/const-eval/enum_discr.rs`;
- `mir/mir_heavy_promoted.rs`;
- two recursive TAIT UI tests;
- `for-loop-while/label_break_value.rs`;
- `RustSmith` seed 7.

`RustSmith` seed 36 is the sole parallel-only flake: it hit the ten-minute limit
in the full gate and passed in isolation.

## Fix discipline

For every compiler fix:

1. Add a minimal `tst/unit/test_*.rs` that is green with the exact Rust 1.90
   compiler before changing mrustc.
2. Confirm the unit is red on current mrustc.
3. Fix the shared compiler path, not the corpus expectation.
4. Run only the new unit, the affected original triggers, and the unit group.
5. Remove completed rows from this file instead of commenting them out.

Do not run another full gate until this file is exhausted. The reclassification
tools are `dev/gate_reclassify.py` and `dev/gate_classify.py`; retained detailed
records are in `/tmp/trustme-reclass-20260814` and
`/tmp/trustme-classification-20260814-final`.
