//@ proc-macro-aux-build: cov99_w286_always_const_bound.rs
#![feature(const_trait_impl)]

#[cov99_w286_always_const_bound::consume]
fn erased<T: const Clone>() {}

fn main() {}
