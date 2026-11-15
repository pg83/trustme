//@ aux-build: cov99_w1074_unsafe_aux.rs
//@ crate-type: lib
//@ compile-only
//@ compile-flags: --emit=metadata
#![feature(fn_delegation)]

pub reuse cov99_w1074_unsafe_aux::increment;
