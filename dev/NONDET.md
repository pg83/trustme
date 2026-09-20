# NONDET.md — compilation must not depend on pointer values or container order

Compiling the same input must give the same result. Two different things break
that property, they are found by different methods, and only one of them was
written down here before. Both are recorded below, with an audit of the sites
that can still bring either back.

## The two classes

**Class A — the result depends on addresses.** A pointer's *value* reaches a
hash, an ordering, or a name. The same binary on the same input then behaves
differently from run to run, because the allocator hands out different
addresses each time.

**Class B — the result depends on the iteration order of an unordered
container.** No address is involved: the hash can be perfectly injective and
keyed on an interner id. What varies is the order the container walks its
entries in, which the standard does not specify. The same binary on the same
input is then *reproducible*, and the divergence only appears between two
builds of the compiler against different C++ standard libraries — libc++ and
libstdc++ choose different bucket counts, growth policies and chain orders.

The distinction matters because it decides how you hunt:

| | run-to-run | libc++ vs libstdc++ | `setarch -R` pins it | found by |
|---|---|---|---|---|
| Class A | varies | varies | yes | local reruns, `setarch -R` |
| Class B | stable | varies | no | the ASan shards in CI, or a permuted-order build |

`setarch -R` is useless against class B: it removes address-space randomisation,
and class B never depended on addresses. That is why both class-B bugs below sat
in the tree while the class-A gate method was known.

## Incident 1 (2026-09-03) — class A: a non-injective pointer hasher

`stl::HashTable` keys its nodes by a `u64` and compares **hashes, not keys**.
A hasher used with it therefore has to be injective: two keys whose hashes
collide are one entry, and `HashMap::insert` releases the node it displaces,
so the later insert silently replaces the earlier one.

`HIRPointerHasher`, which keys `HIRCrate`'s mutable-owner caches, hashed with
`std::hash<const void*>`. libc++ folds bits there, so it is not injective:

```
0x7fe7d17cb490  ->  0eea88268ffc52ce
0x7fe7f17cb490  ->  0eea88268ffc52ce
```

Those are two addresses a live compiler produced for the same offset in two
arenas; they differ only in bit 29. With both items in the cache, one entry
stood for both, and `HIRCrate::findFunctionMut` handed the const evaluator a
different function than the one it asked about — one whose body had not been
lowered. Compilation then aborted in `HIRCrate::getOrGenMir` with "No HIR
(!ep) and no MIR (!ep.m_mir)".

The fix hashes with `splitMix64`, a bijection on `u64`, the way
`stl::IntHasher` already does. `bin/rustc/hir_hir_ut.cpp` pins the property:
the hasher must separate that exact pair, must separate every single-bit
change of an address, and a map keyed by it must keep both colliding items.

A second change went with it: the body of a const-generic argument used to be
named `const_<pointer>#`, so an allocation address reached item paths and
every name generated from them. It is named by creation order now.

## Incidents 2 and 3 (2026-09-20) — class B, found by CI

Both were found the same way, and not on this machine: the 20 ASan shards in
CI build the compiler with apt clang against **libstdc++**, while every local
shell builds it against **libc++**. Two shards failed on inputs that pass
locally and had passed locally for months.

**`81f1a635f` — the vtable slot.** `HIRTrait::valueIndexes`
(`hir_hir.h:480`) is a `std::unordered_multimap<RcString, pair<unsigned,
HIRGenericPath>>`. `getVtableValueIndex` walked `equal_range(name)` and took
the first entry whose *path* matched, ignoring the arguments. For
`trait A: PartialEq<Foo> + PartialEq<Bar>` both occurrences match on path, so
the slot was decided by whichever the container yielded first, and the loser
was handed a `&Bar` where its slot expects a `&Foo`. The emitted C++ then
failed to compile — under libstdc++ only. Fixed by comparing whole trait
references, the way `first_method_vtable_slot` does upstream
(`rustc_trait_selection/src/traits/vtable.rs`).

**`b60e3ed79` — the generic constant's body.** `const CREATE<T: const Create>:
T = T::create();` must have its body checked against `T`. The crate walk
(`Expander::visitConstant`, `hir_conv_constant_evaluation.cpp:5940`) marks such
a constant `Generic` *without* checking it, so whether the body was checked
against `T` or against an instantiated `i32` depended on whether the walk
reached the item before an instantiated use did — that is, on the order
`HIRVisitor::visitModule` (`hir_visitor.cpp:251`) walked `mod.modItems`.
Fixed by checking the body against the constant's own declared type before the
instantiated evaluation.

Note what the hash is **not**: `std::hash<RcString>` is
`rawId() * 0x9E3779B97F4A7C15` (`rc_string.h:127`), an odd multiply — a
bijection — over the interner id. There is nothing wrong with the hasher, and
no address is involved. The order is simply not a property the standard fixes.

## How to find each class

Class A, and the method that found incident 1 — worth repeating:

1. `setarch -R` makes the failure deterministic and stops the reported item
   from moving. That alone proves the dependence is on layout.
2. `-Z dump-hir -Z stop-after=hir` gives byte-identical HIR across runs once
   printed addresses are normalised (`sed -E 's/0x[0-9a-f]{6,}/0xADDR/g'`),
   which clears every pass up to and including `ConvertHIR*`.
3. A one-line trace of every `HIRCrate::getOrGenMir` call (path, plus whether
   HIR and MIR were present) showed the call **sequences were identical** for
   7744 calls and diverged only in the state of the item at the 7745th. So the
   divergence was not in what got compiled but in which object was reached.
4. Printing the item the crate holds at that path next to the one the caller
   passed showed two different objects, at a fixed 512 MiB stride with
   identical low bits — the signature of one lookup returning another item.
5. A probe logging cache hits where the returned pointer differs from the
   requested one named the cache, and a five-line program confirmed the two
   addresses hash equally under libc++.

Steps 3 and 4 are the cheap ones: a call trace is thousands of lines, not the
20M a debug build produces, and it localises the divergence to a single call.

Class B: none of the above applies. Today the only detector is a second build
of the compiler against the other standard library — which is what CI does by
accident, one shard at a time, forty minutes at a time. That is not a method,
it is a coincidence we are living off. See "The gate this still needs".

## Audit — 2026-09-20

Scope: `bin/rustc/**`. Every `std::map`/`set`/`unordered_*` declaration with a
pointer key, and every `std::unordered_*` declaration, was listed and checked
for whether it is iterated. Where it is iterated, the site was read to decide
whether the order is observable. Line numbers drift — regrep the names.

### Class A — still live

- `hir_type.cpp:1383,1389,1395` — `HIRTypeDataNodeType::ord` orders closure,
  generator and async node types by `reinterpret_cast<uintptr_t>`. Equality by
  identity is right; ordering by address is not.
- `ast_path.cpp:94` — `::ord((uintptr_t)v1, (uintptr_t)v2)`.
- `hir_expand_main_bindings.cpp:933` — `std::map<HIRModule*,
  std::vector<NewStatic>> newStatics`, filled at `:5923` and **iterated** at
  `:5944` to create items in each module. Ordered by the module
  object's address, so item creation order follows allocation layout. This is
  the clearest remaining class-A site.
- `hir_typeck_expr_cs.h:155` — `std::map<HIRTypeDataErasedTypeAliasInner*,
  TaitEntry> erasedTypeAliases`, **iterated**.
- `hir_type.cpp:629,700,705,710,715,753,882,887,892` — the type interner mixes
  payload pointers into the hash (`hashMix(h, reinterpret_cast<uintptr_t>(…))`).
  Sound as long as nothing iterates the interner (`hir_type.h:222`), and
  nothing does today.

Pointer-keyed and **not** iterated, so currently harmless — but one added
`for` turns each into a bug: `visited` (`hir_conv_constant_evaluation.cpp:4610`),
`activeAliases` (`hir_from_ast.cpp:186`), `evaluatedConstants`/
`evaluatedStatics`/`processedFunctions` (`trans_monomorphise.cpp:206,207,287`),
`embeddedTags` (`trans_codegen_c.cpp:172`), `knownSpans`
(`expand_proc_macro.cpp:91`), `unencoded`/`exact`/`cache`
(`trans_target.cpp:2220,2221,184`), `objnameCache`
(`hir_serialise_lowlevel.h:23`).

### Class B — observable iteration order

- **The crate walk.** `HIRModule::modItems`, `valueItems`, `macroItems`
  (`hir_hir.h:539,537,541`) are `std::unordered_map<RcString, …>`, and
  `HIRVisitor::visitModule` (`hir_visitor.cpp:251`) walks `modItems` with a
  plain range-for. Every pass that walks a crate therefore visits items in an
  implementation-defined order. Iteration sites: `modItems` 15, `valueItems`
  14, `macroItems` 9. This is the mechanism behind incident 3, and it is the
  one worth fixing at the source rather than per consumer.
- **First-match-wins over external crates.** `HIRCrate::findTraitImplsCb`,
  `findAutoTraitImplsCb`, `findTypeImplsCb` (`hir_hir.cpp:1689,1718,1745`)
  each loop `for (const auto& ec : this->extCrates)` and **return on the first
  crate whose callback answers true**. `extCrates` (`hir_hir.h:839`) is
  unordered. If a lookup can be satisfied by more than one external crate, the
  answer depends on the order — the same shape as incident 2.
- **Last-writer-wins into a symbol table.** `EnumState::enumerateLinkFunctions`
  (`trans_main_bindings.cpp:3429`) walks `crate.extCrates` and, per crate,
  `enumerateLinkFunctionsIn` walks `mod.valueItems` and does
  `linkFunctions[i.linkage.name] = …`. Two items sharing a linkage name give
  whichever the walk reached last.
- **`extCrates` generally.** 17 iteration sites. Not all are observable, and
  they were not all read one by one — this is the largest unfinished part of
  this audit.
- `HIRTrait::valueIndexes` (`hir_hir.h:480`) and its siblings `typeIndexes`
  (`:482`), `assocTypeIndexes` (`hir_expand_main_bindings.cpp:6944`) remain
  unordered. Incident 2 was fixed by making the *comparison* exact, so the
  order no longer decides the answer, but the container still exposes it.
- `hir_from_ast.cpp:3665` — lang items are merged from external crates in the
  order of `crate.externCrates`; `insert` keeps the first, and a genuine
  conflict is an error either way, so the outcome is stable but the text of
  the error names whichever crate came first.

### What is structurally safe

- `stl::HashMap`, `stl::IntMap`, `stl::HashTable` (8 uses in `bin/rustc`)
  expose no iteration at all — there is no `begin()`/`end()`. You cannot make
  this mistake with them. That is an argument for the migration CLAUDE.md
  already asks for, over and above style.
- `HIRTypeRefSet`/`HIRTypeRefMap` (`hir_type_ref.h:26,28`) are
  `std::set`/`std::map` over `const HIRType*` **with `HIRTypeUidOrder`** — the
  interned type's `uid`, described in `hir_type.tu` as "the deterministic
  ordering key for interned types". This is the pattern to copy.

## Mechanisms we already have — do not reinvent

Per `dev/GOAL.md`, the fix for a class of bug is a mechanism, not a patch per
site. Three already exist in this codebase:

1. **`uid` on interned data** + `HIRTypeUidOrder`. Any map or set keyed by an
   interned type should use it; `HIRTypeRefMap` is the alias to reach for.
2. **`HIRCrate::extCratesOrdered`** (`hir_hir.h:837`), a `stl::Vector<RcString>`
   filled in `hir_from_ast.cpp:3657`. It exists precisely because link
   argument order had to be deterministic, and it is used in four places
   (`trans_codegen_c.cpp:1071,1167`, `trans_codegen_mir.cpp:121`,
   `main_bindings.cpp:677`). The other seventeen `extCrates` loops ignore it.
3. **The interner pattern** of `hir_path.cpp`: a pool of nodes, an
   `stl::IntMap` for lookup, and a stable order for iteration. This is what
   `modItems` and friends should become — lookup by name, walk in a fixed
   order.

## The gate this still needs

Nothing in the tree notices when either property rots. A green corpus run does
not predict the next one while the result can vary, and for class B a green
*local* run predicts nothing about CI at all.

What would catch both, cheaply:

- a `-Z` knob that permutes the order in which a module's items are walked
  (reverse, or shuffle by seed), and
- a build node that compiles one fixed input twice, with two different
  permutations, and requires the outputs to be identical — `-C emit-cpp-only`
  compared byte for byte, or `-Z dump-hir` with addresses normalised as in the
  method above.

The bar for that node is concrete: with `81f1a635f` or `b60e3ed79` reverted it
must go red. Class A additionally wants the same input compiled under a couple
of layouts (`setarch -R` pins one; running without it samples others).

Until that exists, the only thing standing between us and the next incident of
either class is that CI happens to build the compiler against a different
standard library than we do.
