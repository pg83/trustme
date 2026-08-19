# Rejections this compiler does not make

A case marked `xfail` in `cases.tsv` is one the Rustonomicon gives as a program
rustc refuses and this compiler accepts. The Rustonomicon is largely *about*
the rules a borrow checker enforces, so most of its failing fences land here.

The row is kept rather than dropped so the case still runs: if the compiler
ever does reject one, its node fails and asks for the row to move back to
`fail`. The same convention as `tst/rust_reference/XFAIL.md`.

## Needs a borrow checker

Region and drop-order analysis. trustme compiles programs; it does not
reproduce every rejection rustc makes.

| case | rule |
|---|---|
| `borrow-splitting__L30.rs` | two mutable borrows of one value through separate fields |
| `dropck__L75.rs` | a value's destructor may observe a reference that has expired |
| `dropck__L146.rs` | as above, through a generic parameter |
| `dropck__L174.rs` | as above, through `#[may_dangle]` |
| `lifetimes__L179.rs` | a `Vec` grown while an element of it is borrowed |
| `ownership__L50.rs` | as above, the chapter's first example |

## Needs lifetimes in the HIR

Lifetimes are erased when the AST is lowered, so nothing downstream can name
the region these ask about.

| case | rule |
|---|---|
| `lifetime-mismatch__L6.rs` | a borrow that outlives the value it came from |
| `lifetime-mismatch__L82.rs` | as above, across a method call |
| `phantom-data__L9.rs` | a lifetime parameter no field uses |
| `subtyping__L99.rs` | `&'long` where `&'short` is wanted, through a mutable reference |
| `subtyping__L181.rs` | as above, through a function pointer's argument |
| `subtyping__L306.rs` | as above, through a `Box<dyn Fn>` |
