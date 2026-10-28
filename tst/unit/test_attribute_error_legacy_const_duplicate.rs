//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: duplicate index

#![no_core]

#[rustc_legacy_const_generics(0, 0)]
fn function<const N: usize>() {}
