//@ crate-type: lib
//@ aux-build: cov99_w1058_const_bounds.rs
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(fn_delegation, const_trait_impl)]

pub reuse cov99_w1058_const_bounds::source as delegated;
