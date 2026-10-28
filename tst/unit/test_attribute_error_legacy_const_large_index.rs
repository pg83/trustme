//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: too large

#![no_core]

#[rustc_legacy_const_generics(4294967295)]
fn function<const N: usize>() {}
