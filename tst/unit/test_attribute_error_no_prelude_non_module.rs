//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: Invalid use of #[no_prelude] on non-module

#![no_core]

#[no_prelude]
struct Value;
