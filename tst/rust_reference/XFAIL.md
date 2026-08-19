# Rejections this compiler does not make

A case marked `xfail` in `cases.tsv` is one the Rust Reference gives as a
program rustc refuses and this compiler accepts. The program's *behaviour* is
not in question — only the diagnostic is missing, and every one below needs
machinery this compiler does not have and is not going to grow.

The row is kept rather than dropped so the case still runs: if the compiler
ever does reject one, its node fails and asks for the row to move back to
`fail`.

## Needs a borrow checker

Region and drop-order analysis. trustme compiles programs; it does not
reproduce every rejection rustc makes.

| case | rule |
|---|---|
| `destructors__L462.rs` | a temporary dropped while still borrowed |
| `destructors__L558.rs` | as above, through a block's tail |
| `destructors__L566.rs` | as above |
| `destructors__L575.rs` | as above |
| `destructors__L589.rs` | as above |
| `destructors__L596.rs` | as above |
| `destructors__L603.rs` | as above |
| `expressions/array-expr__L98.rs` | the temporary an index expression borrows ends with the statement |
| `expressions/operator-expr__L201.rs` | the temporary a `Deref` call borrows ends with the statement |
| `items/unions__L168.rs` | two mutable borrows of one union, through different fields |
| `types/closure__L294.rs` | a capture by `ImmBorrow` outlives the mutable borrow beside it |
| `types/closure__L375.rs` | as above, through a slice pattern |
| `types/closure__L518.rs` | as above, through `addr_of!` on a packed field |

## Needs lifetimes in the HIR

Lifetimes are erased when the AST is lowered, so nothing downstream can name
the region these ask about.

| case | rule |
|---|---|
| `lifetime-elision__L140.rs` | the lifetime bound of a trait object cannot be deduced here |
| `lifetime-elision__L206.rs` | a return reference's lifetime is not pinned to one argument |
| `trait-bounds__L188.rs` | `T: 'a` is not implied by the signature |

## Needs imports resolved as macros expand

rustc interleaves import resolution with macro expansion, and the rules below
are about what was and was not visible at each step: a candidate that only
appeared once a macro had run, or one glob's name measured against another's
while both were still being worked out. This compiler expands every macro
first and then resolves, so it has no such order to ask about -- and where the
question does not arise, neither does the ambiguity.

| case | rule |
|---|---|
| `names/name-resolution__L265.rs` | a glob's name against an outer one, in a `use` |
| `names/name-resolution__L285.rs` | as above, in the macro namespace |
| `names/name-resolution__L332.rs` | a textual `macro_rules` scope against a path-based import |
| `names/name-resolution__L391.rs` | a macro definition written by a macro against one that was not |
| `names/name-resolution__L467.rs` | two macro definitions written by two expansions of one macro |
| `names/name-resolution__L482.rs` | as above, for a module reached through a `use` |
