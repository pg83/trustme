//@ compile-fail: Unspecified parameter with no default
#![feature(generic_const_items)]

const MARK<T>: u8 = 0;

fn take(MARK: u8) {}

fn main() {}
