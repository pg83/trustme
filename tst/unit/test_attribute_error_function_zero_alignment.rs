//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: alignment must be non-zero

#![no_core]

#[rustc_align(0)]
fn function() {}
