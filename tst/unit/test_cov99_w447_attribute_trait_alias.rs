//@ proc-macro-aux-build: cov99_w447_item_identity.rs
//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(trait_alias)]

#[cov99_w447_item_identity::keep]
trait Alias = Copy;

fn main() {}
