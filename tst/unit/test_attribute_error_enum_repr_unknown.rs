//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: Unknown enum repr

#![no_core]

#[repr(unknown)]
enum Value { One }
