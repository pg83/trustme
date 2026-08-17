# Full-gate failure triage and fix priorities

This file contains unfinished work only. Priorities are ordered by the number
of independently reproduced failures that a shared fix can plausibly remove.
Source locations are routing signatures, not proof of a shared root cause.

Snapshot: 2026-08-17, commit `10754f0e0`. The numbers below come from rerunning
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
environment, most recently at the commit above. The authoritative rerun data is
in `/tmp/trustme-reclass-20260817d`; classified records are in
`/tmp/trustme-classification-20260817d`. Every count below is measured from
that rerun, not decremented by hand.

Reruns and the two whole-group sweeps (`rust_ui_compile`, `rust_1_90`) are the
only regression check there is between full gates, and they earn their keep: the
sweeps found two regressions from this session's own work (a rejected
`#![recursion_limit = "0"]`, and a dropped `#[cfg]` on a first parameter).

| result | tests |
|---|---:|
| total active fast-gate nodes | 14,113 |
| failed in the full gate | 631 |
| still failing on the current tree | 323 |
| fixed, or no longer reproducing, since the gate | 308 |

| priority class | tests |
|---|---:|
| accepted Rust rejected by the compiler or driver | 126 |
| compiler BUG, MIR TODO/ERROR, assertion, exception, or signal | 80 |
| wrong runtime behaviour, panic, abort, or output | 41 |
| missing rejection or diagnostic | 53 |
| generated C++ or link failure | 15 |
| stable timeout | 8 |

## P0: accepted Rust rejected by the front end

All 126 tests are positive programs accepted by Rust 1.90. A normal trustme
error is a compiler deficiency, not an expected corpus result.

| shared area | tests | largest routes |
|---|---:|---|
| parser | 32 | 29 unexpected-token failures through the three `parse_parseerror.cpp` routes; 3 `parse_common.cpp` failures |
| type checking, HIR lowering, and resolution | 89 | trait/impl selection 30 (`hir_typeck_expr_cs.cpp:6746`, `:6748`); unresolved type/value names 6 (`resolve_main_bindings.cpp:395`, `:403`); type mismatch 16 (`hir_typeck_expr_cs.cpp:2471`, `:2482`, `:2487`) |
| macro and attribute expansion | 7 | attributes 4; macro parsing 3 |
| CTFE and MIR lowering | 6 | constant evaluation 4; move/scope lowering 2 |
| crate/driver handling | 2 | missing external crate path 1; enum repr 1 |

The 29 parser failures must be regrouped by syntax family before changing the
parser; the common `parse_parseerror.cpp` line is only the reporting site. By
unexpected token the largest families are never patterns (3) and a long tail of
one- and two-test spellings. Grouping by test directory finds them faster than
grouping by token: that is how the six associated-const equality bounds turned
out to be one syntax rule.

The whole `gen` family is fixed. `gen fn` and `gen { .. }` lower to the
coroutine `iter!` builds, wrapped in `from_coroutine(..).fuse()`. `async gen`
lowers to the async block's coroutine with `isAsyncGen` set, which gives it a
generated `AsyncIterator` impl (`poll_next` returning `Poll<Option<Item>>`)
instead of a `Future` one, makes a `yield` return `Poll::Ready(Some(v))` the way
`.await` returns `Poll::Pending`, and makes the END state return
`Poll::Ready(None)` so the iterator is fused. `for await pat in it` desugars
like `for`, but through `IntoAsyncIterator::into_async_iter` and an await of
`AsyncIterator::poll_next` (`HIRExprNodeAWait::isNext`). What is left is
`gen { .. }` as an expression outside a macro fragment
(`parse_common.cpp:1422`): in source `gen` is a contextual keyword, so an
expression-position `gen {` has to be edition-gated against a struct literal --
no corpus test needs it today.

An async closure's future now takes the captures with it instead of borrowing
the frame of the call that made it, which is what made a capture read freed
stack. What is still wrong is a capture the closure holds by *reference*: the
future takes a copy of the referent rather than the reference, so a write
through it is lost (`async |i| { seen += i; i }` leaves `seen` untouched).
rustc solves this with a second, by-reference coroutine body; we have one body.

The 46 tests routed through the trait-selection and type-mismatch lines are not
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

There are 80 compiler-internal failures in 67 stable signatures.

| compiler area | tests |
|---|---:|
| type checker | 20 |
| MIR lowering, CTFE MIR, and optimisation | 17 |
| HIR lowering and conversion | 14 |
| parser and macro expansion | 12 |
| translation and code generation | 10 |
| name resolution | 4 |
| routed by a bare `ASSERT`/signal line with no file attribution | 7 |

The multi-test signatures are:

| signature | tests |
|---|---:|
| `ASSERT` with no backtrace | 5 |
| eleven other two-test signatures | 22 |
| fifty-seven one-test signatures | 57 |

The line numbers in a signature move with every commit that touches the file:
the ones here are read from the classification named above, and are worth
re-deriving rather than trusting. Attribution needs the crashing thread's
frames, which is not thread 1 any more -- the compiler runs on a thread of its
own for the bigger stack, so `dev/gate_classify.py` now picks the thread that
took the signal.

## P1: runtime semantics

Forty-one programs build but execute incorrectly:

| runtime result | tests | note |
|---|---:|---|
| Rust panic, exit 101 | 35 | group by the failed semantic assertion, never by exit code |
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

The let-chain family is now one bug, and a measured one. Each `&&` operand has
its own temporary scope, which it did not before; what is left is that a binding
from a `let` operand only drops when that operand is the *last* one. For
`drop_order_let_chain` the collected order is
`[1..12, 14, 15, 16, 19..22]` where rustc gives `[1..23]`: 13, 17, 18 and 23 are
the bindings of non-final `let` operands, and their drops never run. The guard
lowering pushes a variable scope per guard and terminates them all at the end
(`mir_from_hir.cpp`, the `scopes` stack), and the drop *is* emitted -- with a
drop flag that is cleared in the same basic block, immediately before the check:

```text
bb14: { df0 = 0; df1 = 0; if(!df0) goto bb18; ... drop var4 ... }
```

So the question is which scope resets those flags to their defaults there;
`endSplitArm` and the state merge around `mir_from_hir.cpp:9130` are where the
arms' variable states are reconciled. Minimal case, which gives `[1, 3]` where
rustc gives `[1, 3, 2]`:

```rust
if let Some(_d) = log.loud(2) && log.loud(1).is_some() { log.mark(3); }
```

Grouping the panics by the rule they check finds these multi-test families:

| family | tests | rule |
|---|---:|---|
| let-chain drop order | 3 | `drop_order_let_chain`, `drop_order_if_let_rescope`, `drop-order-comparisons-let-chains`: a binding from a `let` operand that is followed by another operand is never dropped at all |
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

Most of this class is one missing pass. Roughly twenty of the fifty-three want
a borrow checker: the seven `destructors` ones are temporaries dropped while
still borrowed, and the Rustonomicon ones (`lifetime-mismatch`, `subtyping`,
`dropck`, `borrow-splitting`, `ownership`) are region and drop-order rules. No
diagnostic in that group is reachable without one, so do not count them as
independent work. The next largest group is reachable: eight `name-resolution`
and `scopes` tests want an ambiguity error, raised when a name that two glob
imports (or a glob and an outer item) both provide is *used*.

`abi/variadic-ffi` is the other `...` test and needs more than parsing: a named
variadic parameter (`mut ap: ...`) is a `VaListImpl` the body then reads, so
dropping the parameter as the unnamed form does would only move the failure.

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

Eight nodes still time out, but timing each one alone shows that most are not
slow -- the harness gives compile and run 60 seconds each, and under the gate's
load a crash or a hang reads as a timeout. Measured at commit `ae3b4ed4b`:

| node | measured | what it really is |
|---|---|---|
| `mir/mir_heavy_promoted.rs` | 93s compile, runs fine | genuinely slow |
| `consts/large-zst-array-77062.rs` | over 300s | genuinely slow |
| `enum-discriminant/discriminant_value.rs` | aborts after 33s | `cannot infer a type satisfying _: Debug` inside `assert_eq!` |
| `deriving/issue-58319.rs` | aborts after 24s | MIR optimisation does not converge (`mir_operations.cpp:1677`, 100 passes) on a derived `Clone` |
| `consts/const-eval/enum_discr.rs` | runs, asserts | no longer recurses; a variant that names a *later* variant still reads zero, because the expression's MIR is const-folded in place and a second pass sees the folded value |
| `impl-trait/recursive-type-alias-impl-trait-declaration-too-subtle-2.rs` | SIGSEGV in 3s | unbounded recursion resolving a type alias that names itself |
| Exercism `palindrome-products` | not measured | |
| RustSmith seed 7 | not measured | |

`for-loop-while/label_break_value.rs` was a runtime hang, and
`parser/survive-peano-lesson-queue.rs` a stack overflow; both are fixed.

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
