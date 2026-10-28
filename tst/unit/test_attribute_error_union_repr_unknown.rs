//@ crate-type: lib
//@ compile-flags: -Zstop-after=expand
//@ compile-fail: Unknown union repr

#![no_core]

#[repr(unknown)]
union Value { field: u32 }
