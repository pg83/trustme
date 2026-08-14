# Full-gate failure triage and fix priorities

This file contains unfinished work only.

Snapshot: 2026-08-13, working tree on top of fa97e261d. The full gate was run
as `./build -B .build-clang -j 78 -k test` in the clang Nix environment:

| result | tests |
|---|---:|
| total | 14,520 |
| passed | 13,057 |
| failed | 1,463 |

Every failed node was then run independently with the same command and its own
log. 1,462 failures reproduce; only RustSmith seed 97 passes in isolation.
Source locations below are stable failure signatures, not automatically root
causes: a generic reporting location can contain several unrelated bugs.

| class | tests |
|---|---:|
| compiler BUG, MIR TODO/ERROR, assertion, exception, SIGSEGV/SIGILL | 408 |
| ordinary front-end rejection of a positive test | 688 |
| generated C++/assembly/link failure | 52 |
| wrong runtime result or abort | 145 |
| missing rejection/diagnostic | 73 |
| missing native test-helper link input | 31 |
| adapter/ordinary exit 1 | 56 |
| stable timeout | 9 |
| load-only flake | 1 |

## P1 — internal compiler failures

After the targeted CTFE rerun, 261 of the snapshot's internal compiler failures
remain. The generic `mir_helpers.h:108` signature must be subdivided by its
message before fixing.

| signature | tests | note |
|---|---:|---|
| CTFE panic, `hir_conv_constant_evaluation.cpp:3617` | 8 | independent compile-time assertions/dead-code cases |
| MIR error, `mir_helpers.h:108` | 31 | mixed intrinsics, pointer operations, raw DSTs and SIMD |
| CTFE intrinsic TODO, `hir_conv_constant_evaluation.cpp:3498` | 16 | `black_box`, `forget`, `raw_eq`, pointer offsets, SIMD and comparisons |
| BUG, `macro_rules_macro_rules.cpp:2189` | 10 | |
| CTFE unresolved generic, `hir_conv_constant_evaluation.cpp:1751` | 9 | const-generic inference/unevaluated expressions |
| BUG, `hir_hir.cpp:1653` | 8 | |
| BUG, `parse_common.cpp:1404` | 5 | |
| assertion, `hir_expr_ptr.cpp:130` | 5 | |
| seven signatures with four failures each | 28 | `trans_codegen_c`, `synext_macro`, typeck, MIR, bad-char parsing, HIR path assertion |
| 99 smaller signatures | 141 | one to three tests each |

The signal failures are included above: two SIGILLs at
`mir_operations.cpp:1441`, two SIGSEGVs at
`hir_conv_main_bindings.cpp:1430`, and four one-test SIGSEGV signatures.

## P2 — accepted Rust 1.90 front-end features

The 688 ordinary rejections split by compiler area:

| area | tests | largest signatures |
|---|---:|---|
| parser | 283 | 147 at `parse_parseerror.cpp:63`, 107 at line 56, 19 at line 68 |
| type checking/name and HIR resolution | 330 | failed impl 47, mismatch 41, TAIT trait method 32, missing method 29, associated output 24 |
| macro/attribute expansion | 69 | `expand_common.cpp:465` 24, `synext_decorator.cpp:2492` 13 |
| MIR/CTFE rejection | 6 | |

The parser totals are not three bugs. The largest concrete syntax families are
async/closure function bodies (32), return-type notation and other `..` forms
(15), `reuse` delegation (23), lifetime-in-path forms (10), `become` (9), and
the remaining long tail of Rust 1.90 syntax.

## P3 — diagnostics, backend output and runtime semantics

- 73 negative tests compile successfully: 60 Rust Reference cases and 13
  Nomicon cases. Group these by the missing diagnostic before implementing them.
- 30 tests produce C++ rejected by clang: incomplete generated types (7), wrong
  `main` ABI (4), enum narrowing (3), pointer-to-small-integer casts (3), bad
  assignments (3), undeclared generated symbols (2), and eight smaller cases.
- Three additional tests reach the linker and fail on unresolved/intentional
  native symbols.
- 102 executables panic with exit 101. The common `assert_eq!` text covers many
  unrelated semantic failures and is not a root-cause cluster.
- Repairing the shared CTFE arithmetic path exposed eight former compiler
  failures as runtime mismatches: seven `core::num::dec2flt` tests and
  `core::num::int_log::ilog10_u128`.
- 12 executables abort: 11 Miri cases and one allocation failure in a library
  test.
- Two repaired native-helper link failures now abort in
  `rust_dbg_extern_empty_struct`; the generated SysV call mishandles a C empty
  struct between two register-heavy arguments.
- 12 output mismatches: nine async-drop tests and RustSmith seeds 15, 19, 102.
- One remaining adapter exit is a GCCRS torture test whose expected non-zero
  runtime status does not match the produced program.

## P4 — performance and flakes

Nine nodes time out in isolation:

- RustSmith seeds 7 and 36 at the ten-minute node limit;
- `consts/large-zst-array-77062.rs` and two recursive TAIT UI tests;
- `for-loop-while/label_break_value.rs`, `deriving/issue-58319.rs`, and
  `consts/const-eval/enum_discr.rs`;
- Exercism `palindrome-products`.

RustSmith seed 97 is the sole load-only flake: it failed the parallel gate but
completed in an isolated rerun close to its ten-minute limit.

## Work order

Fix by shared impact, not by corpus order:

1. The remaining P1 clusters, largest verified root cause first.
2. Generated-C++ families, runtime semantic families, then front-end features.
3. Missing diagnostics and isolated long-tail failures.

For every compiler change: add a minimal `tst/unit/test_*.rs` that is green on
Rust 1.90, confirm it is red on current mrustc, fix the shared path, then run
only the affected triggers and the unit corpus. Remove completed entries from
this file. Do not run another full gate until the file is exhausted.

Reclassification tools: `dev/gate_reclassify.py` reruns commands extracted from
a gate log; `dev/gate_classify.py` emits per-case records and stable signature
clusters.
