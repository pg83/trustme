//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: #[global_allocator] on non-static

#![no_core]

#[global_allocator]
fn function() {}
