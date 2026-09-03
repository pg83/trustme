# NONDET.md — compilation must not depend on pointer values

Compiling the same input twice used to give different results: one run
succeeded, the next aborted. Disabling address-space randomisation made the
outcome stable, which said the divergence was driven by the addresses the
allocator handed out rather than by anything in the input.

Found and fixed 2026-09-03. The mechanism, the method that found it, and the
sites that could bring it back are recorded here because the property has no
gate yet.

## What it was

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

Whether two live items collide depends on where the allocator put them, which
is the whole of the run-to-run variation.

The fix hashes with `splitMix64`, a bijection on `u64`, the way
`stl::IntHasher` already does. `bin/rustc/hir_hir_ut.cpp` pins the property:
the hasher must separate that exact pair, must separate every single-bit
change of an address, and a map keyed by it must keep both colliding items.

A second change went with it: the body of a const-generic argument used to be
named `const_<pointer>#`, so an allocation address reached item paths and
every name generated from them. It is named by creation order now.

## How it was found

Worth repeating, because the same method will find the next one.

1. `setarch -R` made the failure deterministic and stopped the reported item
   from moving. That alone proves the dependence is on layout.
2. `-Z dump-hir -Z stop-after=hir` gave byte-identical HIR across runs once
   printed addresses were normalised (`sed -E 's/0x[0-9a-f]{6,}/0xADDR/g'`),
   which cleared every pass up to and including `ConvertHIR*`.
3. A one-line trace of every `HIRCrate::getOrGenMir` call (path, plus whether
   HIR and MIR were present) showed the call **sequences were identical** for
   7744 calls and diverged only in the state of the item at the 7745th. So the
   divergence was not in what got compiled but in which object was reached.
4. Printing the item the crate holds at that path next to the one the caller
   passed showed two different objects, at a fixed 512 MiB stride with
   identical low bits — the signature of one lookup returning another item.
5. A probe in `findFunctionMut` logging cache hits where the returned pointer
   differs from the requested one named the cache, and a five-line program
   confirmed the two addresses hash equally under libc++.

Steps 3 and 4 are the cheap ones: a call trace is thousands of lines, not the
20M a debug build produces, and it localises the divergence to a single call.

## What is still unguarded

The codebase deliberately uses raw pointers as handles and pointer identity as
equality (see CLAUDE.md), which is fine. It stops being fine wherever a
pointer's *value* reaches a hash, an ordering, or an iteration order. Interned
types carry a `uid` for exactly this reason ("the deterministic ordering key
for interned types", `hir_type.tu`); the sites below have no such key and have
not been audited since the fix above.

Ordering by address:

- `hir_type.cpp:1382,1388,1394` — `HIRTypeDataNodeType::ord` orders closure,
  generator and async node types by `reinterpret_cast<uintptr_t>`. Equality by
  identity is right; ordering by address is not.
- `hir_typeck_expr_cs.h` — `std::map<HIRTypeDataErasedTypeAliasInner*, …>`.
- `hir_expand_main_bindings.cpp` — `std::map<HIRModule*, …> newStatics`,
  iterated to create items, so its order decides item order.
- `ast_path.cpp:94` — `::ord((uintptr_t)v1, (uintptr_t)v2)`.

Hashing by address:

- `hir_type.cpp:628,699,704,709,714,752,881,886,891` — the type interner mixes
  payload pointers into the hash. Sound as long as nothing iterates the
  interner, which nothing does today.
- `hir_typeck_helpers.cpp` — `structuralCertaintyCache_` is keyed by the
  interned type's address through `IntMap`, which is safe: `IntHasher` is
  `splitMix64` and `HashTable` compares the full 64-bit key.

## The gate this still needs

Compile one fixed input under a handful of layouts and require identical
output. `setarch -R` pins one layout; running without it samples others. The
property is worth a build node, because nothing else will notice when it rots:
a green corpus run does not predict the next one while it can vary.
