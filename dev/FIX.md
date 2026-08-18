# Full-gate failure triage and fix priorities

This file contains unfinished work only. Priorities are ordered by the number
of independently reproduced failures that a shared fix can plausibly remove.
Source locations are routing signatures, not proof of a shared root cause.

Snapshot: 2026-08-18, commit `4094b67bc`. The numbers below come from rerunning
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
in `/tmp/trustme-reclass-20260818g`; classified records are in
`/tmp/trustme-classification-20260818g`. Every count below is measured from
that rerun, not decremented by hand.

Reruns and the whole-group sweeps are the only regression check there is
between full gates, and they earn their keep: a sweep of `rust_ui_compile
rust_1_90 rust_reference rust_by_example gccrs gccrs_compile miri` found the
one regression this session's parser work introduced (`impl<T> ::path::Trait`
read as a qualified path), which a rerun of the failing set could not have.

| result | tests |
|---|---:|
| total active fast-gate nodes | 14,113 |
| failed in the full gate | 631 |
| still failing on the current tree | 221 |
| fixed, or no longer reproducing, since the gate | 410 |

The 221 is measured, not decremented by hand: the eight corpus groups that hold
the failures (`rust_ui_compile rust_1_90 rust_reference rust_by_example gccrs
gccrs_compile miri rust_lib`) are rerun whole, which is also the only regression
check there is between full gates. That run last stood at 196; the remaining 25
are in groups outside it and come from the last full sweep.

| priority class | tests |
|---|---:|
| accepted Rust rejected by the compiler or driver | 72 |
| compiler BUG, MIR TODO/ERROR, assertion, exception, or signal | 55 |
| wrong runtime behaviour, panic, abort, or output | 38 |
| missing rejection or diagnostic | 53 |
| generated C++ or link failure | 10 |
| stable timeout | 8 |

## P0: accepted Rust rejected by the front end

All 72 tests are positive programs accepted by Rust 1.90. A normal trustme
error is a compiler deficiency, not an expected corpus result.

| shared area | tests | largest routes |
|---|---:|---|
| type checking, HIR lowering, and resolution | 61 | trait/impl selection 26; type mismatch 14; unresolved type/value names 5 |
| parser | 7 | unexpected-token failures through the three `parse_parseerror.cpp` routes |
| macro and attribute expansion | 1 | |
| CTFE and MIR lowering | 2 | |
| crate/driver handling | 1 | |

The 7 parser failures must be regrouped by syntax family before changing the
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

The 40 tests routed through the trait-selection and type-mismatch lines are not
one root cause: fixing integer inference through an operator took two of them
and left the rest untouched. Minimise each before grouping.


The `IntoIterator for Box<[T]>` family is fixed. What made those four tests
fail was the probe committing while the receiver was still unknown:
`Box::new(boxed_slice).into_iter()` probes `Box<_>`, where every step matches
`IntoIterator` fuzzily, and the by-value step it settled on is wrong once the
type arrives. A candidate that only matches because the receiver is unknown
now makes the probe wait, unless it is the only candidate (a lone one is the
answer whatever the type becomes) or type checking has stabilised, where there
is nothing left to wait for. Only a type with no inference class at all counts
as unknown: a literal's type is settled by fallback whatever the method is, and
pausing on one loses the integer that an index expression needs.

`self: SmartPtr<Self>` still fails
(`arbitrary_self_types_lifetime_elision.rs`, `_niche_deshadowing.rs`): the
receiver is `SmartPtr<_>` when the probe runs, and the inherent-method cache is
keyed on the receiver's first type argument, so it is asked for a key it does
not have. Offering every impl under that path was tried: the method is then
found, but its `Self` is still an ivar and the path cannot be resolved
(`Failed to locate function <_>::m`).

A `for<T>` binder is only dropped where it quantifies a where predicate. In a
supertrait list (`trait Foo: for<T> Bar<T>`) or a return type
(`impl for<T> Trait<T>`) the bound is the item's whole meaning, so
`non_lifetime_binders/method-probe.rs` and `on-rpit.rs` still fail. Replacing
the binder's argument with a wildcard was tried and reverted: it makes both
tests reach typeck and then abort on an inference variable that a bound may
not hold (`hir_typeck_common.cpp`, `allowInfer`).

`trait S = ?Sized;` is supported: the relaxed bound adds nothing to the alias,
so expanding the alias shortens the list it was in, and a trait object whose
principal trait was such an alias takes the trait that followed it. Note that
Rust 1.97 rejects a relaxed bound in a trait alias outright ("nothing to
relax"), so the reference compiler cannot confirm the 1.90 behaviour these tests
want -- the unit test covers the expansion with a lifetime-only alias instead.

What is left of the unresolved-name group splits by rule: two
`non_lifetime_binders` (the `for<T>` trap above), `T` in a const trait bound
under `-Zunpretty=hir` (which runs the passes that build the HIR, so it cannot
stop before resolution), `Self` as a constructor from an item nested inside the
impl, and a `use` of an item declared in the same block.

The two `issue-65041-empty-vis-matcher` tests are a trap: accepting a
visibility on an *enum variant* lets them parse further and then crash inside
the macro engine, on a `$vis` fragment that expanded to nothing. The clean
parse error they give today is the better failure until that is fixed. A trait
item is not affected -- it takes a `$vis` fragment now.

## P1: internal compiler failures

There are 55 compiler-internal failures in 53 stable signatures.

| compiler area | tests |
|---|---:|
| type checking, HIR lowering, and name resolution | 24 |
| MIR lowering, CTFE MIR, and optimisation | 17 |
| translation and code generation | 7 |
| macro expansion | 4 |
| routed by a bare `ASSERT` line with no file attribution | 2 |
| parser | 1 |

Only two signatures cover more than one test, so the class is a long tail:

| signature | tests |
|---|---:|
| `ASSERT` with no backtrace | 2 |
| two two-test signatures | 4 |
| forty-nine one-test signatures | 49 |

The line numbers in a signature move with every commit that touches the file:
the ones here are read from the classification named above, and are worth
re-deriving rather than trusting. Attribution needs the crashing thread's
frames, which is not thread 1 any more -- the compiler runs on a thread of its
own for the bigger stack, so `dev/gate_classify.py` now picks the thread that
took the signal.

## P1: runtime semantics

Thirty-eight programs build but execute incorrectly:

| runtime result | tests | note |
|---|---:|---|
| Rust panic, exit 101 | 32 | group by the failed semantic assertion, never by exit code |
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

The let-chain family is fixed. Each `&&` operand has its own temporary scope, and
a binding from a `let` operand now drops at the end of the body whether or not
another operand follows it. What was wrong is worth remembering, because it is a
trap the next state bug will look like: the operand that fails drops what the
operands before it bound, and that exit is a *branch* -- the value states it left
behind were the ones the body then saw, so the body's own drop was emitted with a
flag the exit had already cleared. `terminateScopeEarly` now keeps the states when
the exit crosses a conditional, which is what a frozen scope already did for the
same reason.

Grouping the panics by the rule they check finds these multi-test families:

| family | tests | rule |
|---|---:|---|
| coroutine and future size | 4 | `niche-in-coroutine`, `overlap-locals`, `resume-arg-size`, `future-as-arg`: locals that cannot be live together must share storage |
| `TypeId` of a higher-ranked type | 2 | `type-id-higher-rank` and the `core::any` library case: not reachable, see below |
| `Waker::will_wake` | 2 | two library cases comparing a cloned waker's vtable |

The two `TypeId` ones are not reachable at all: they require `fn(&'static isize)`
and `for<'a> fn(&'a isize)` to have different type ids, and this compiler erases
lifetimes -- `HIRPathParams` has no lifetime list to carry them. Nothing short of
carrying lifetimes through HIR would separate those types, so do not count these
two as independent work.

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
