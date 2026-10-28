//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: Unknown inline type

#![no_core]

#[inline(sometimes)]
fn function() {}
