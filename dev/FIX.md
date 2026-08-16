# Full-gate failure triage and fix priorities

This file contains unfinished work only. Priorities are ordered by the number
of independently reproduced failures that a shared fix can plausibly remove.
Source locations are routing signatures, not proof of a shared root cause.

Snapshot: 2026-08-16, commit `b8c247ba4`. The numbers below come from rerunning
every node that failed the last full gate, not from a fresh gate. The gate
itself ran at commit `79582dd3f` in the clang Nix environment on all 78
available cores:

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
`/tmp/trustme-reclass-20260816-nix`; classified records are in
`/tmp/trustme-classification-20260816-nix`.

| result | tests |
|---|---:|
| total active fast-gate nodes | 14,113 |
| failed in the full gate | 631 |
| still failing on the current tree | 587 |
| fixed, or no longer reproducing, since the gate | 44 |

| priority class | tests |
|---|---:|
| accepted Rust rejected by the compiler or driver | 296 |
| compiler BUG, MIR TODO/ERROR, assertion, exception, or signal | 101 |
| wrong runtime behaviour, panic, abort, or output | 90 |
| missing rejection or diagnostic | 58 |
| generated C++ or link failure | 33 |
| stable timeout | 9 |

## P0: accepted Rust rejected by the front end

All 296 tests are positive programs accepted by Rust 1.90. A normal trustme
error is a compiler deficiency, not an expected corpus result.

| shared area | tests | largest routes |
|---|---:|---|
| parser | 137 | 134 unexpected-token failures through the three `parse_parseerror.cpp` routes; 3 `parse_common.cpp` failures |
| type checking, HIR lowering, and resolution | 120 | trait/impl selection 29 (`hir_typeck_expr_cs.cpp:6693`, `:6695`); unresolved type/value names 18 (`resolve_main_bindings.cpp:395`, `:403`); type mismatch 15 (`hir_typeck_expr_cs.cpp:2468`, `:2479`) |
| macro and attribute expansion | 31 | macro parsing/formatting 10; attributes 8; AST expansion 8; other expansion 5 |
| CTFE and MIR lowering | 6 | constant evaluation 4; move/scope lowering 2 |
| crate/driver handling | 2 | missing external crate path 1; pathless `--extern` 1 |

The 134 parser failures must be regrouped by syntax family before changing the
parser; the common `parse_parseerror.cpp` line is only the reporting site. By
unexpected token the largest families are `gen` blocks and functions (8),
`default`/specialization items (6), lifetime binders (6), and a long tail of
one- and two-test spellings.

## P1: internal compiler failures

There are 101 compiler-internal failures in 75 stable signatures.

| compiler area | tests |
|---|---:|
| type checker | 29 |
| HIR lowering and conversion | 25 |
| MIR lowering, CTFE MIR, and optimisation | 13 |
| parser and macro expansion | 12 |
| translation and code generation | 10 |
| unattributed (assert or signal with no backtrace) | 8 |
| name resolution | 4 |

The multi-test signatures are:

| signature | tests |
|---|---:|
| `BUG hir_typeck_common.cpp:704` | 5 |
| `ASSERT` with no backtrace | 5 |
| `BUG hir_typeck_common.cpp:687` | 3 |
| `BUG hir_typeck_static.cpp:3636` | 3 |
| `BUG hir_conv_constant_evaluation.cpp:4586` | 3 |
| twelve other shared signatures | 24 |
| fifty-eight one-test signatures | 58 |

`BUG hir_conv_constant_evaluation.cpp:4586` is the polymorphic-constant
assertion `[T; Generic(N)]`, reached by the two
`generic_const_parameter_types` tests now that `_` const arguments infer.

## P1: runtime semantics

Ninety programs build but execute incorrectly:

| runtime result | tests | note |
|---|---:|---|
| Rust panic, exit 101 | 83 | group by the failed semantic assertion, never by exit code |
| stdout mismatch | 3 | RustSmith seeds 19 and 102; async-drop ordering |
| abort with no backtrace | 2 | packed-drop double panic, library allocation failure |
| generated executable SIGABRT | 1 | |
| compiled, exited 1 | 1 | |

The repeated high-yield areas inside the panic set are integer/NonZero and
128-bit operations, formatting and type-name behaviour, enum/DST/layout, drop
order, and coroutine layout. Minimise representatives before treating nearby
assertions as one root cause.

## P2: missing language checks

Fifty-eight negative tests compile successfully: 45 Rust Reference cases and
13 Nomicon cases. The largest source areas are:

| language area | tests |
|---|---:|
| diagnostic attributes | 3 |
| name resolution | 7 |
| destructor restrictions | 7 |
| trait items | 2 |
| const evaluation | 3 |
| subtyping | 3 |
| trait bounds | 3 |
| closure restrictions | 3 |
| drop checking | 3 |
| all smaller areas | 26 |

Source chapters are routing information. Group the concrete examples by the
missing language rule before implementing diagnostics.

The three `diagnostics` survivors need what `unused_must_use` did not bring:
`#[deny]`/`#[allow]` on an item rather than the crate (`:235`), the
`unsafe_code` lint (`:256`), and the error for lifting a `forbid` (`:92`).

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

Nine nodes still time out in isolated reruns:

- Exercism `palindrome-products`;
- `enum-discriminant/discriminant_value.rs`;
- `consts/large-zst-array-77062.rs`;
- `consts/const-eval/enum_discr.rs`;
- `mir/mir_heavy_promoted.rs`;
- `impl-trait/recursive-type-alias-impl-trait-declaration-too-subtle-2.rs`;
- `for-loop-while/label_break_value.rs`;
- `deriving/issue-58319.rs`;
- RustSmith seed 7.

Twenty-six of the gate's failures pass when rerun on the current tree. Most
are the fixes recorded above; the rest were parallel-only, RustSmith seed 36
among them.

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

`resvg` is outside this file: its node is wrapped in the 60-second
`TEST_TIMEOUT` (`build.py`), which no from-source project build can meet, so it
cannot pass regardless of the compiler.

Do not run another full gate until this file is exhausted. Reclassification is
performed by `dev/gate_reclassify.py` inside the clang Nix environment and then
by `dev/gate_classify.py`.
