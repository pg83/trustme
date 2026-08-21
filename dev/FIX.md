# Full-gate failure triage and fix priorities

This file contains unfinished work only. Priorities are ordered by the number
of independently reproduced failures that a shared fix can plausibly remove.
Source locations are routing signatures, not proof of a shared root cause.

The baseline full gate ran at commit `79582dd3f` in the clang Nix environment
on all 78 available cores; its last complete reclassification snapshot was
made on 2026-08-18 at commit `4094b67bc`:

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

All 631 failed nodes were independently rerun for that snapshot. A fresh
whole-group eight-corpus sweep on 2026-08-21 found 98 failures before the
latest fix. Its current-compiler rerun data is in
`.build-clang/reclass-20260821c`; classified records are in
`.build-clang/classification-20260821c`. One node was green in that
classification, and the subsequent point reruns of `niche-in-coroutine`,
`link-directives`, `fn-align-dyn`, gccrs `issue-2187`, and const-generic
promotion, `deriving-with-repr-packed`, and `match-ref-mut-stability` are green
after their fixes; the specialization impl-head fix also closed
`specialization-basics` and `impl-trait/equality-rpass`, and the macro type
fragment fix closed `macro-bare-trait-object-maybe-trait-bound`; deferring an
identity `DiscriminantKind` projection until numeric fallback also closed
`enum-discriminant/discriminant_value`; preserving arbitrary trait-alias
where-clauses through argument-position `impl Trait` also closed
`traits/alias/bounds`; completing default const arguments in trait impl heads
closed `const-generics/defaults/rp_impl_trait` and
`const-generics/defaults/trait_objects`. Thus 83 of the 98 sweep failures
remain. The 25 failures outside those corpora are still carried from the
complete snapshot rather than silently dropped from the total.

Reruns and the whole-group sweeps are the only regression check there is
between full gates. Sweep all eight groups (`rust_ui_compile rust_1_90
rust_reference rust_by_example gccrs gccrs_compile miri rust_lib`): a
whole-group sweep can find regressions that rerunning only known failures
cannot.

| result | tests |
|---|---:|
| total active fast-gate nodes | 14,115 |
| failed in the full gate | 631 |
| still failing or still carried from the last full sweep | 108 |
| fixed, or no longer reproducing, since the gate | 523 |

The eight corpus groups that hold most failures (`rust_ui_compile rust_1_90
rust_reference rust_by_example gccrs gccrs_compile miri rust_lib`) were rerun
whole on 2026-08-21. The sweep found 98 failures before the latest fixes; the
subsequent point fixes have closed fifteen nodes, leaving 83. The remaining 25 are
in groups outside that sweep and are still carried from the last full sweep.

| current eight-corpus result | tests |
|---|---:|
| accepted Rust rejected by the compiler or driver | 47 |
| compiler BUG, MIR TODO/ERROR, assertion, exception, or signal | 22 |
| wrong runtime behaviour, panic, abort, or output | 11 |
| stable timeout | 3 |
| carried from groups outside the sweep | 25 |

## P0: accepted Rust rejected by the front end

The current eight-corpus rerun has 47 positive programs accepted by Rust 1.90.
A normal trustme error is a compiler deficiency, not an expected corpus
result.

| shared area | tests | largest routes |
|---|---:|---|
| type checking, HIR lowering, and resolution | 44 | trait/impl selection and type mismatch dominate |
| CTFE and MIR lowering | 2 | if-let guards |
| parser | 1 | named variadic parameter |

An async closure's future now takes the captures with it instead of borrowing
the frame of the call that made it, which is what made a capture read freed
stack. What is still wrong is a capture the closure holds by *reference*: the
future takes a copy of the referent rather than the reference, so a write
through it is lost (`async |i| { seen += i; i }` leaves `seen` untouched).
rustc solves this with a second, by-reference coroutine body; we have one body.

The tests routed through the trait-selection and type-mismatch lines are not
one root cause. Minimise each before grouping.


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

What is left of the unresolved-name group splits by rule: two
`non_lifetime_binders` (the `for<T>` trap above), `T` in a const trait bound
under `-Zunpretty=hir` (which runs the passes that build the HIR, so it cannot
stop before resolution), `Self` as a constructor from an item nested inside the
impl, and a `use` of an item declared in the same block.

## P1: internal compiler failures

There are 22 compiler-internal failures in 21 stable signatures in the current
eight-corpus rerun.

| compiler area | tests |
|---|---:|
| type checking and HIR lowering | 13 |
| MIR lowering, CTFE MIR, and optimisation | 5 |
| translation and code generation | 3 |
| unattributed compiler abort | 1 |

Only one signature covers more than one test, so the class is a long tail:

| signature | tests |
|---|---:|
| `BUG hir_typeck_common.cpp:824` | 2 |
| twenty one-test signatures | 20 |

The line numbers in a signature move with every commit that touches the file:
the ones here are read from the classification named above, and are worth
re-deriving rather than trusting. Attribution needs the crashing thread's
frames, which is not thread 1 any more -- the compiler runs on a thread of its
own for the bigger stack, so `dev/gate_classify.py` now picks the thread that
took the signal.

## P1: runtime semantics

Eleven programs in the current eight-corpus rerun build but execute
incorrectly:

| runtime result | tests | note |
|---|---:|---|
| Rust panic, exit 101 | 9 | group by the failed semantic assertion, never by exit code |
| stdout mismatch | 1 | async-drop ordering |
| generated executable SIGABRT | 1 | library allocation failure |

The repeated high-yield areas inside the panic set are enum/DST/layout, drop
order, and coroutine layout. Two remaining `type_name` failures concern the
anonymous scope: an item declared inside a function body sits under `#N` in
its path, where rustc names it after the function, so `issue-61894` still
prints `issue_61894::#0::f` for
`issue_61894::Bar<_>::foo::f` and `any::dyn_type_name` prints `any::#2::Foo`.
Naming those scopes is a parser change (`ASTModule::addAnon`) that moves every
such path, and with it every mangled name. Separately,
`test_format_int_exp_precision` still fails. Minimise representatives before
treating nearby assertions as one root cause.

Grouping the panics by the rule they check finds these multi-test families:

| family | tests | rule |
|---|---:|---|
| coroutine and future size | 3 | `overlap-locals`, `resume-arg-size`, `future-as-arg`: locals that cannot be live together must share storage |
| `TypeId` of a higher-ranked type | 2 | `type-id-higher-rank` and `any-lifetime-escape-higher-rank`: not reachable, see below |

The two `TypeId` ones are not reachable at all: they require `fn(&'static isize)`
and `for<'a> fn(&'a isize)` to have different type ids, and this compiler erases
lifetimes -- `HIRPathParams` has no lifetime list to carry them. Nothing short of
carrying lifetimes through HIR would separate those types, so do not count these
two as independent work.

## P2: isolated front-end rules

`abi/variadic-ffi` is the other `...` test and needs more than parsing: a named
variadic parameter (`mut ap: ...`) is a `VaListImpl` the body then reads, so
dropping the parameter as the unnamed form does would only move the failure.

`_` is written the same way for a type and for a const argument, and lowering
splits a path's arguments into a type list and a value list by that spelling
alone, so `Foo::<_, 1>` on `Foo<const N: bool, const M: u8>` binds `N` to `1`.
`fixParamCount` moves a surplus placeholder across afterwards, but by then the
order the arguments were written in is gone, so it can only append.
`inferred_const` and `infer_arg_and_const_arg` need the classification done
where the order survives -- in `LowerHIRPathParams`, against the item's own
parameter list, which means threading the declaration in. Recording the
placeholder in both lists and pruning later was tried and reverted: everything
that reads a path's parameters before `Resolve Bind` (type-alias expansion,
constant evaluation of a generic argument) sees the extra entry and rejects it.

A function item and something else meeting in one `if`/`match` settle on the
item, and the other arm is then read as the item too. `let f: fn(u8) = if c {
fallback } else { transmute(p) }` calls `fallback` whatever `p` holds, because
the transmute's target became the item's zero-sized type and the load was
dropped; that is why `set_alloc_error_hook` has no effect on
`handle_alloc_error` (`std::alloc::rust_oom` reads `HOOK` and then discards it),
which `alloctests`' `test_shrink_to_unwind` measures. rustc gives the arms a
least upper bound and decays a function item to a pointer where the arms
differ; here the first arm equates the result ivar to the item and the rest
follows.

Offering the decayed pointer beside the item as a coercion possibility was
tried and reverted: it fixes the case above, but the possibility resolver then
has two function-pointer types to order where it had none, and `core`'s
`Filter::next_chunk` -- `const { if needs_drop { a } else { b } }` over two
function items -- stops on a mismatch between them. The fix belongs in the
match/`if` result type, as a least upper bound over the arms, not in the
pairwise coercion.

## P3: performance and flakes

Three nodes in the current eight-corpus rerun time out even in isolation. The
harness gives compile and run 60 seconds each:

| node | measured | what it really is |
|---|---|---|
| `coroutine/issue-87142.rs` | over 60s | not yet minimised |
| `consts/large-zst-array-77062.rs` | over 300s | genuinely slow |
| `impl-trait/recursive-type-alias-impl-trait-declaration-too-subtle-2.rs` | over 60s | unbounded recursive alias resolution |

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
