//@ crate-type: lib
//@ compile-fail: error
#![feature(no_core, generic_const_items, generic_const_exprs)]
#![no_core]
pub const ARRAY<T>: [u8; {
    let pointer = 0usize as *const T;
    let _ = pointer;
    0
}] = [];
