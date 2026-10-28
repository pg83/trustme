//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: #[thread_local] on `const`

#![no_core]

#[thread_local]
const VALUE: u32 = 1;
