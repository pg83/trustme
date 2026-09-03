# NONDET.md — the compiler's output depends on pointer values

Compiling the same input twice can give different results: one run succeeds,
the next aborts. Disabling address-space randomisation makes the outcome
stable, so the divergence is driven by the addresses the allocator hands out,
not by anything in the input.

Measured 2026-09-03 on `aea2b5ca2`, x86_64, inside `nix develop .#clang`.

## Reproducing it

`tst/rust_lib` builds one library test at a time from the vendored Rust 1.90
sources. Any of its `coretests` cases will do; this one is the smallest that
still flips:

```
python3 tst/rust_lib/case.py coretests num module \
    coretests/tests/num/mod.rs 2024 num/mod.rs test_empty num::test_empty \
    tst/rust_lib/upstream <libstd.tar> <rust-lib-dependencies.tar> /tmp/stamp
```

Run it a few times. Roughly half the runs abort with

```
BUG: ASSERT FAIL: bin/rustc/hir_hir.cpp:1648:ep.mir:
    No HIR (!ep) and no MIR (!ep.m_mir) for ::"bin#"::num::u64::#74::compiletime
```

and the rest compile and pass. The item named is not stable either: over four
runs it was `num::u64::#74::compiletime`, then `num::i32::#324::compiletime`,
then no failure at all, twice.

Prefix the compiler invocation with `setarch -R` and the same command fails
**every** time, always on `num::u64::#74::compiletime`. That is the whole
diagnosis in one line: the compiler is a pure function of its input plus the
address-space layout.

## Where the divergence is not

- **Not the input.** `-Z dump-hir -Z stop-after=hir` produces byte-identical
  HIR across runs once the addresses the dumper prints are normalised
  (`sed -E 's/0x[0-9a-f]{6,}/0xADDR/g'`). Everything up to and including the
  `ConvertHIR*` passes is already deterministic.
- **Not one bad test.** Deleting the test that fails first only moves the
  failure to another module (`u16` → `u64`). Cutting `num/mod.rs` down to
  `u64` alone makes it pass, so the trigger is the accumulated state of a
  large crate, not any single item.

So the divergence starts inside `ConvertHIRConstantEvaluate` and the
on-demand pipeline it drives through `HIRCrate::getOrGenMir`.

## Why it matters

This is the single largest source of failures in the test corpus. A clean
`./build lite_tests` on this machine reports 256 failing nodes, of which 225
are compiler aborts, and the aborts group as:

| site | log lines | note |
| --- | --- | --- |
| `hir_hir.cpp:1648` | 194 | this bug (≈97 nodes; the wrapper logs each twice) |
| `hir_typeck_common.cpp:446` | 36 | "Impl parameters were not expected" |
| `trans_mangling.cpp:385` | 34 | "Non-encodable type"; also flaky, see below |
| `hir_typeck_helpers.cpp:1843` | 16 | |
| everything else | ≤8 each | |

The same non-determinism shows up compiling libcore directly: in one batch of
serial runs the baseline compiler aborted twice out of nine with
`trans_mangling.cpp:385: Non-encodable type Dst/*I:1*/`, and the other seven
runs of the identical command were clean. Any timing or pass/fail measurement
that does not pin the layout is measuring the layout.

Until this is fixed, "the corpus is green" is not a statement that can be
made: a green run does not predict the next one.

## Candidate causes

The codebase deliberately uses raw pointers as handles and pointer identity
as equality (see CLAUDE.md), which is fine on its own. It stops being fine
wherever a pointer's *value* reaches an ordering, a hash, or an iteration
order, because that value is what randomisation moves. Interned types already
carry a `uid` for exactly this reason ("the deterministic ordering key for
interned types", `hir_type.tu`); the sites below have no such key.

Ordering by address:

- `hir_type.cpp:1382,1388,1394` — `HIRTypeDataNodeType::ord` orders closure,
  generator and async node types by `reinterpret_cast<uintptr_t>`. Equality by
  identity is right; ordering by address is not.
- `hir_typeck_expr_cs.h:113` — `std::map<HIRTypeDataErasedTypeAliasInner*, …>`.
- `hir_expand_main_bindings.cpp:931` — `std::map<HIRModule*, …> newStatics`,
  iterated to create items, so the order decides item order.
- `ast_path.cpp:94` — `::ord((uintptr_t)v1, (uintptr_t)v2)`.

Hashing by address:

- `hir_type.cpp:628,699,704,709,714,752,881,886,891` — the type interner mixes
  payload pointers into the hash, so bucket layout follows the heap.
- `hir_typeck_helpers.cpp:12109` — `structuralCertaintyCache_` is keyed by the
  interned type's address (documented in SOLVER.md). A memo keyed by address is
  only safe while it stays a pure memo.
- `expand_proc_macro.cpp:91`, `trans_target.cpp:2220-2221` — `unordered_map`
  keyed by pointer.

None of these is confirmed as *the* cause yet; they are the places where a
pointer value can become an observable decision.

## Suggested attack

1. Make the failure deterministic (`setarch -R`) and reduce the input until a
   debug-build trace is small enough to keep. Reduction has to preserve crate
   scale, so cut whole test modules rather than single items.
2. Find a second layout that is also deterministic but takes the other branch
   — a fixed-size padding environment variable under `setarch -R` is enough to
   shift the heap without reintroducing randomness — and diff the two traces
   with addresses normalised. The first differing line names the decision.
3. Fix that site by giving the handle a creation-order key, the way interned
   types already have one, rather than by sorting or hashing its address.
4. Add a gate: compile one fixed input under a handful of `setarch -R`
   paddings and require identical output. Without such a gate the property
   will rot again.
