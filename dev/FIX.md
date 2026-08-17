# Full-gate failure triage and fix priorities

This file contains unfinished work only. Priorities are ordered by the number
of independently reproduced failures that a shared fix can plausibly remove.
Source locations are routing signatures, not proof of a shared root cause.

Snapshot: 2026-08-17, commit `544a0d06d`. The numbers below come from rerunning
the nodes that failed the last full gate, not from a fresh gate. The gate
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

All 631 failed nodes were rerun independently inside the same clang Nix
environment; the 412 that were still red were then rerun again after the fixes
recorded below. The authoritative rerun data is in
`/tmp/trustme-reclass-20260817c`; classified records are in
`/tmp/trustme-classification-20260817c`. These counts are measured, not
decremented by hand, except for the failures fixed after that rerun: four
parser ones (two `ergonomic-clones`, two `tokens.md` literal suffixes) and nine
generated-code ones.

| result | tests |
|---|---:|
| total active fast-gate nodes | 14,113 |
| failed in the full gate | 631 |
| still failing on the current tree | 357 |
| fixed, or no longer reproducing, since the gate | 274 |

| priority class | tests |
|---|---:|
| accepted Rust rejected by the compiler or driver | 151 |
| compiler BUG, MIR TODO/ERROR, assertion, exception, or signal | 88 |
| wrong runtime behaviour, panic, abort, or output | 40 |
| missing rejection or diagnostic | 53 |
| generated C++ or link failure | 15 |
| stable timeout | 10 |

## P0: accepted Rust rejected by the front end

All 151 tests are positive programs accepted by Rust 1.90. A normal trustme
error is a compiler deficiency, not an expected corpus result.

| shared area | tests | largest routes |
|---|---:|---|
| parser | 41 | 38 unexpected-token failures through the three `parse_parseerror.cpp` routes; 3 `parse_common.cpp` failures |
| type checking, HIR lowering, and resolution | 95 | trait/impl selection 30 (`hir_typeck_expr_cs.cpp:6701`, `:6703`); unresolved type/value names 6 (`resolve_main_bindings.cpp:395`, `:403`); type mismatch 13 (`hir_typeck_expr_cs.cpp:2468`, `:2479`) |
| macro and attribute expansion | 7 | attributes 4; macro parsing 3 |
| CTFE and MIR lowering | 6 | constant evaluation 4; move/scope lowering 2 |
| crate/driver handling | 2 | missing external crate path 1; enum repr 1 |

The 38 parser failures must be regrouped by syntax family before changing the
parser; the common `parse_parseerror.cpp` line is only the reporting site. By
unexpected token the largest families are `async gen` blocks and functions (7),
never patterns (3), and a long tail of one- and two-test spellings. Grouping by test directory finds them faster than
grouping by token: that is how the six associated-const equality bounds turned
out to be one syntax rule.

The `gen` family is down to the 7 tests that need `async gen`. `gen fn` and
`gen { .. }` lower to the coroutine `iter!` already builds, wrapped in
`from_coroutine(..).fuse()`; `async gen` has no such wrapper in core, so it
needs a generated `AsyncIterator` impl beside the generated `Coroutine` one
(`hir_expand_main_bindings.cpp`, where the coroutine struct is made).
`gen { .. }` as an expression is still only reachable from a macro fragment
(`parse_common.cpp:1422`): in source `gen` is a contextual keyword, so an
expression-position `gen {` has to be edition-gated against a struct literal.

The 48 tests routed through the trait-selection and type-mismatch lines are not
one root cause: fixing integer inference through an operator took two of them
and left the rest untouched. Minimise each before grouping.

`IntoIterator for Box<[T]>` is four of them (two `into-iter-on-*-lint`, two
`into-iter-on-boxed-slices-*`) and one bug in the method probe. Two attempts
that did not help, so that they are not repeated:

- making the `rustc_skip_during_method_dispatch(boxed_slice)` gate fire for a
  receiver that is still `Box<_>` only moves the failure one deref step on, to
  `[T]` taken by value;
- rejecting a by-value receiver whose type is unsized
  (`checkMethodReceiver`, `Receiver::Value`) makes a fourth test fail as well.

The probe accepts a by-value `into_iter(self)` candidate on `[T]`, which then
fails `requireSized` (`hir_typeck_expr_cs.cpp:4662`) instead of the probe moving
on to the autoref step where `IntoIterator for &[T]` waits. Find why that
candidate is accepted before changing the gate again.

A `for<T>` binder is only dropped where it quantifies a where predicate. In a
supertrait list (`trait Foo: for<T> Bar<T>`) or a return type
(`impl for<T> Trait<T>`) the bound is the item's whole meaning, so
`non_lifetime_binders/method-probe.rs` and `on-rpit.rs` still fail.

`trait S = ?Sized;` is a trap of the same kind: parsing the relaxed bound
gets past the parser and then asserts in `hir_conv_main_bindings.cpp:1539`,
because an alias that names no trait is not expandable where a trait is
expected. The parse error is the better failure until an alias can expand
to another alias.

What is left of the unresolved-name group splits by rule: two
`non_lifetime_binders` (the `for<T>` trap above) and four one-offs
(`UnitLike` in an enum body, `T` in a const trait bound, `Self` as a
constructor, a type alias used as a value).

The two `issue-65041-empty-vis-matcher` tests are a trap: accepting a
visibility on an *enum variant* lets them parse further and then crash inside
the macro engine, on a `$vis` fragment that expanded to nothing. The clean
parse error they give today is the better failure until that is fixed. A trait
item is not affected -- it takes a `$vis` fragment now.

## P1: internal compiler failures

There are 88 compiler-internal failures in 71 stable signatures.

| compiler area | tests |
|---|---:|
| HIR lowering and conversion | 23 |
| type checker | 20 |
| MIR lowering, CTFE MIR, and optimisation | 12 |
| parser and macro expansion | 12 |
| translation and code generation | 10 |
| name resolution | 4 |
| routed by a bare `ASSERT`/signal line with no file attribution | 7 |

The multi-test signatures are:

| signature | tests |
|---|---:|
| `ASSERT` with no backtrace | 5 |
| `BUG hir_conv_constant_evaluation.cpp:4670` | 3 |
| eleven other two-test signatures | 22 |
| fifty-eight one-test signatures | 58 |

`BUG hir_conv_constant_evaluation.cpp:4670` is the polymorphic-constant
assertion `[T; Generic(N)]`: the three `generic_const_parameter_types` tests
declare a const parameter whose own type is generic.

## P1: runtime semantics

Forty programs build but execute incorrectly:

| runtime result | tests | note |
|---|---:|---|
| Rust panic, exit 101 | 34 | group by the failed semantic assertion, never by exit code |
| stdout mismatch | 3 | RustSmith seeds 19 and 102; async-drop ordering |
| abort with no backtrace | 2 | packed-drop double panic, library allocation failure |
| generated executable SIGABRT | 1 | |

The repeated high-yield areas inside the panic set are enum/DST/layout, drop
order, and coroutine layout. `macro-doc-raw-str-hashes` is the last
`stringify!` one: a `meta` fragment still prints as a placeholder rather than
the attribute it holds. `issue-61894` is the type name of a function item,
which still prints as `fn{::"bin#"::#0::f}` -- the path of a function inside an
impl is not reconstructed. Of the formatting ones, `test_format_int_exp_precision` survives the precision
fix. No 128-bit ones are left. Minimise representatives before treating nearby
assertions as one root cause.

Grouping the panics by the rule they check finds these multi-test families:

| family | tests | rule |
|---|---:|---|
| let-chain drop order | 3 | `drop_order_let_chain`, `drop_order_if_let_rescope`, `drop-order-comparisons-let-chains`: a non-`let` operand's temporary in a chain drops at the end of the chain, a `let` binding's lives for the block |
| coroutine and future size | 4 | `niche-in-coroutine`, `overlap-locals`, `resume-arg-size`, `future-as-arg`: locals that cannot be live together must share storage |
| `TypeId` of a higher-ranked type | 2 | `type-id-higher-rank` and the `core::any` library case |
| `Waker::will_wake` | 2 | two library cases comparing a cloned waker's vtable |

`will_wake` is a trap: it compares vtable *addresses*, and `alloc::task`
promotes the same `RawWakerVTable` value in two functions (`const#0` and
`const#1`), which we emit as two weak symbols. rustc relies on the backend
merging them and documents the check as best-effort -- upstream even skips the
tests under Miri. Merging equal promoted constants (never `static` items, whose
addresses Rust does keep distinct) is the fix, and it has to survive separate
translation units.

`glossary__L232` reached this class from the generated-code one: an enum with a
single unit variant needs no tag, so `size_of` of it is 0 and we say 1. The
data-enum path already collapses a lone variant (`trans_target.cpp`, the
`e.size() <= 1` branch); the value-enum path beside it still emits a tag. Only
`repr(Rust)` collapses -- `#[repr(u8)]` keeps one byte and `#[repr(C)]` four.

## P2: missing language checks

Fifty-three negative tests compile successfully. The largest source areas are:

| language area | tests |
|---|---:|
| name resolution | 7 |
| destructor restrictions | 7 |
| trait items | 2 |
| const evaluation | 3 |
| subtyping | 3 |
| trait bounds | 3 |
| closure restrictions | 3 |
| drop checking | 3 |
| all smaller areas | 23 |

Source chapters are routing information. Group the concrete examples by the
missing language rule before implementing diagnostics.

The two-argument `wrap_binder!(e; T)` form is not reachable: our macro engine
does not match `($expr:expr ; $ty:ty)`. No corpus test uses it.

`limits__L37` is the other half of `#![recursion_limit]`: the limit now bounds
macro expansion, and that test needs it to bound auto-dereferencing as well
(`(|_: &u8| {})(&&&1)` with a limit of 1).

## P2: generated code and linking

Nine tests emit C++ rejected by clang: three inline-assembly lowerings, two
enum-discriminant narrowings, one malformed generated filename `-.cpp`, and
three remaining incomplete or wrongly ordered generated types.

Six tests reach the linker: three miss generated constant symbols, two refer
to intentional native test symbols, and one exercises native-link directives.

## Regressions since the gate

The gate is the baseline, so a node that passed then and fails now is a
regression, not an old failure. A rerun of every corpus at commit `6de2b19ef`
found none, and 371 failures in total: the 370 counted above plus RustSmith seed
36, which is parallel-only. An earlier rerun at commit `d2d0065e1` had found
eight regressions, all since fixed:

- `builtin_macro_concat` printed `2.15` with every digit binary128 can need;
- `irrefutable-path`, `issue-34751` and `unpretty-expr-fn-arg` were rejected by
  the refutable-parameter check;
- `const-negation` read a folded negative minimum as a positive magnitude;
- `issue-30756` was linted for `thread_local!`'s own `unsafe`;
- `if-block-unreachable-expr` and `ice-zst-static-access` lost the divergence of
  a match whose every arm diverges.

Rerun the corpora after a batch of fixes: each of these came from a fix that
the unit and libstd checks passed.

## P3: performance and flakes

Ten nodes still time out in isolated reruns:

- Exercism `palindrome-products`;
- `enum-discriminant/discriminant_value.rs`;
- `consts/large-zst-array-77062.rs`;
- `consts/const-eval/enum_discr.rs`;
- `mir/mir_heavy_promoted.rs`;
- `impl-trait/recursive-type-alias-impl-trait-declaration-too-subtle-2.rs`;
- `for-loop-while/label_break_value.rs`;
- `deriving/issue-58319.rs`;
- `parser/survive-peano-lesson-queue.rs`, which used to die on a stack
  overflow instead;
- RustSmith seed 7.

Two hundred and thirty-seven of the gate's failures pass when rerun on the
current tree. Most are the fixes recorded above; the rest were parallel-only,
RustSmith seed 36 among them.

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

A generic type that derives `Copy` and `Clone` still clones field by field:
`*self` needs `Self: Copy`, which the bounds a derived `Clone` adds do not
give, and emitting it breaks the standard library.

`resvg` is outside this file: its node is wrapped in the 60-second
`TEST_TIMEOUT` (`build.py`), which no from-source project build can meet, so it
cannot pass regardless of the compiler.

Do not run another full gate until this file is exhausted. Reclassification is
performed by `dev/gate_reclassify.py` inside the clang Nix environment and then
by `dev/gate_classify.py`.
