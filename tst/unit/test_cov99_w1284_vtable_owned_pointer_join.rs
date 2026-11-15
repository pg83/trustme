//@ compile-only
//@ compile-flags: -Zstop-after=typeck -Zdump-hir -Zdump-ast
//@ proc-macro-aux-build: cov99_w1284_vtable_tokens.rs
//@ aux-build: cov99_w1284_vtable_trait.rs

extern crate cov99_w1284_vtable_trait;
extern crate cov99_w1284_vtable_tokens;

fn main() {
    let _ = cov99_w1284_vtable_tokens::vtable!();
}
