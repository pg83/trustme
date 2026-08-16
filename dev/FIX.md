# Full-gate failure triage and fix priorities

This file contains unfinished work only. Priorities are ordered by the number
of independently reproduced failures that a shared fix can plausibly remove.
Source locations are routing signatures, not proof of a shared root cause.

Snapshot: 2026-08-16, commit `79582dd3f`. The full gate ran in the clang Nix
environment on all 78 available cores:

```text
nix --extra-experimental-features 'nix-command flakes' develop .#clang -c \
  ./build -B .build-clang -j 78 -k test
```

Full log: `/tmp/trustme-full-gate-20260815.log`.

Before rerunning failures, the current CAS dependency roots were published so
the rerun could not pick up an older global compiler or standard library:

```text
nix --extra-experimental-features 'nix-command flakes' develop .#clang -c \
  ./build -B .build-clang -j 78 -k \
  rustc cargo libstd rust_test_helpers rust_lib_dependencies
```

All 631 failed nodes were then rerun independently inside the same clang Nix
environment. The authoritative rerun data is in
`/tmp/trustme-reclass-20260815-nix`; classified records are in
`/tmp/trustme-classification-20260815-nix`.

| result | tests |
|---|---:|
| total active fast-gate nodes | 14,113 |
| passed in the full gate | 13,482 |
| failed in the full gate | 631 |
| failures reproduced independently | 630 |
| parallel-only flakes | 1 |
| unfinished after focused fixes | 620 |

| priority class | tests |
|---|---:|
| accepted Rust rejected by the compiler or driver | 312 |
| wrong runtime behaviour, panic, abort, signal, or output | 96 |
| compiler BUG, MIR TODO/ERROR, assertion, exception, or signal | 93 |
| missing rejection or diagnostic | 76 |
| generated C++ or link failure | 33 |
| stable timeout | 9 |
| parallel-only flake | 1 |

## P0: accepted Rust rejected by the front end

All 312 tests are positive programs accepted by Rust 1.90. A normal trustme
error is a compiler deficiency, not an expected corpus result.

| shared area | tests | largest routes |
|---|---:|---|
| type checking, HIR lowering, and resolution | 136 | trait/impl selection 32; unresolved type/value names 18; `_` assignment lowering 9; receiver/Deref lowering 4 |
| parser | 137 | 134 unexpected-token failures through the three `parse_parseerror.cpp` routes; 3 slice-pattern/parser-common failures |
| macro and attribute expansion | 31 | macro parsing/formatting 10; attributes 9; AST expansion 8; other expansion 4 |
| CTFE and MIR lowering | 6 | constant evaluation 4; move/scope lowering 2 |
| crate/driver handling | 2 | missing external crate path 1; pathless `--extern` 1 |

The 134 parser failures must be regrouped by syntax family before changing the
parser; the common `parse_parseerror.cpp` line is only the reporting site. The
largest visible families include new `gen` syntax, unsafe binders, closure
binders, specialization/default items, parser recovery cases, and macro
interpolation.

## P1: runtime semantics

Ninety-six programs compile but execute incorrectly:

| runtime result | tests | note |
|---|---:|---|
| Rust panic, exit 101 | 83 | group by the failed semantic assertion, never by exit code |
| generated executable assertion | 5 | SIMD `reduce_add`, `round`, and `fabs`; two SysV empty-struct ABI cases |
| stdout mismatch | 3 | RustSmith seeds 19 and 102; async-drop ordering |
| abort | 3 | NLL case, packed-drop double panic, library allocation failure |
| segmentation fault | 2 | coroutine issue-69039 and resume-live-across-yield |

The repeated high-yield areas inside the panic set are integer/NonZero and
128-bit operations, formatting and type-name behaviour, enum/DST/layout, drop
order, and coroutine layout. Minimise representatives before treating nearby
assertions as one root cause.

## P1: internal compiler failures

There are 93 compiler-internal failures in 74 stable signatures.

| compiler area | tests |
|---|---:|
| type checker | 29 |
| HIR lowering and conversion | 25 |
| MIR lowering, CTFE MIR, and optimisation | 13 |
| parser and macro expansion | 12 |
| translation and code generation | 10 |
| name resolution | 4 |

The multi-test signatures are:

| signature | tests |
|---|---:|
| `BUG hir_typeck_common.cpp:704` | 5 |
| `BUG hir_typeck_common.cpp:687` | 3 |
| `BUG hir_typeck_static.cpp:3636` | 3 |
| `BUG/ASSERT/SIG*` at eleven other shared signatures | 22 |
| sixty one-test signatures | 60 |

The remaining internal set includes ten compiler signals, five MIR errors,
five MIR TODOs, and three uncaught exceptions. Generated-program signals and
assertions are counted under runtime semantics, not here.

## P2: missing language checks

Seventy-six negative tests compile successfully: 63 Rust Reference cases and
13 Nomicon cases. The largest source areas are:

| language area | tests |
|---|---:|
| diagnostic attributes | 9 |
| inline assembly validation | 8 |
| name resolution | 7 |
| destructor restrictions | 7 |
| trait items | 4 |
| const evaluation | 3 |
| subtyping | 3 |
| trait bounds | 3 |
| closure restrictions | 3 |
| drop checking | 3 |
| all smaller areas | 26 |

Source chapters are routing information. Group the concrete examples by the
missing language rule before implementing diagnostics.

## P2: generated code and linking

Twenty-seven tests emit C++ rejected by clang:

| generated-code family | tests |
|---|---:|
| incomplete, missing, or wrongly ordered generated types | 10 |
| invalid aggregate or value assignment | 5 |
| invalid `main` ABI in `no_std` programs | 4 |
| enum-discriminant narrowing | 3 |
| inline assembly lowering | 3 |
| missing 128-bit intrinsic lowering | 1 |
| malformed generated filename `-.cpp` | 1 |

Six tests reach the linker: three miss generated constant symbols, two refer
to intentional native test symbols, and one exercises native-link directives.

## P3: performance and flakes

Nine nodes time out in isolated reruns:

- Exercism `palindrome-products`;
- `enum-discriminant/discriminant_value.rs`;
- `consts/large-zst-array-77062.rs`;
- `consts/const-eval/enum_discr.rs`;
- `mir/mir_heavy_promoted.rs`;
- `impl-trait/recursive-type-alias-impl-trait-declaration-too-subtle-2.rs`;
- `for-loop-while/label_break_value.rs`;
- `deriving/issue-58319.rs`;
- RustSmith seed 7.

RustSmith seed 36 is the sole parallel-only flake: it timed out in the full
gate and passed in the independent Nix rerun.

## Fix discipline

For every compiler fix:

1. Add a minimal `tst/unit/test_*.rs` that is green with the exact Rust 1.90
   compiler before changing trustme.
2. Confirm that unit is red on current trustme.
3. Fix the shared compiler path, not the corpus expectation.
4. Run only the new unit, the affected original triggers, and the unit group.
5. Remove completed rows from this file instead of commenting them out.
6. Decrement a baseline node only once; a latent failure exposed after an
   earlier root disappears is not an additional old-gate node.

Do not run another full gate until this file is exhausted. Reclassification is
performed by `dev/gate_reclassify.py` inside the clang Nix environment and then
by `dev/gate_classify.py`.
