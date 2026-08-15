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

## Internal compiler failures

There are 102 unfinished compiler-internal failures in 82 stable signatures.

| signature or root cause | tests | note |
|---|---:|---|
| 82 stable signatures | 102 | one to three tests each |

The eight signal failures are two SIGILLs at `mir_operations.cpp:1473`, two
SIGSEGVs at `hir_conv_main_bindings.cpp:1430`, the recursive pattern-lowering
SIGSEGV at `mir_from_hir.cpp:4760`, and three other one-test SIGSEGV signatures.
Four uncaught exceptions and five explicit MIR TODOs are included in the 102.

## Accepted Rust rejected by the front end

The 215 unfinished ordinary compiler rejections and one pathless-`--extern`
driver rejection split as follows:

| area | tests | largest stable groups |
|---|---:|---|
| parser | 53 | remaining failures at `parse_parseerror.cpp` and `parse_common.cpp` require syntax-family regrouping |
| type checking, HIR lowering, and resolution | 125 | async closure `Fn*` selection 1; `IntoFuture` await selection 1; diverging-pattern match 1 |
| macro and attribute expansion | 31 | other expansion and attribute failures |
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

Thirty tests produce C++ rejected by clang. Repeated generated-code
families are incomplete or unknown generated types (8), invalid `main` ABI
(4), invalid assignments (4), enum-discriminant narrowing (3), and inline
assembly (3). Eight smaller failures remain.

Six tests reach the linker: three miss generated constant symbols, two refer to
intentional native symbols, and one exercises native-link directives.

## Runtime semantics

- 81 executables panic with exit 101. The panic exit itself is not a root
  cause; group these by the repeated semantic failure before fixing them.
- Two outputs differ: RustSmith seeds 19 and 102.
- Three executables abort: two async Miri double-frees and one library test.
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
