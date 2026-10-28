//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: #[unsafe(no_mangle)] on bad item

#![no_core]

#[unsafe(no_mangle)]
struct Value;
