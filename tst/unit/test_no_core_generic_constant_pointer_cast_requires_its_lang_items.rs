//@ compile-fail: error
#![feature(no_core, generic_const_items)]
#![no_core]
const ADDRESS<T>: *const T = 0 as *const T;
fn main() {}
