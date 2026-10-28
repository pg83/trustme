//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: alignment must be a power of two

#![no_core]

#[rustc_align(3)]
fn function() {}
