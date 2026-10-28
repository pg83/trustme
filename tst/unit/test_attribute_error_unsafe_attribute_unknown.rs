//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: Unknown #[unsafe

#![no_core]

#[unsafe(unknown)]
fn function() {}
