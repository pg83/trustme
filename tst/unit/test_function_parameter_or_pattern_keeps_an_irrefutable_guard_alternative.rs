//@ compile-variant: -O
#![feature(guard_patterns)]
#![allow(incomplete_features)]
fn next(((value if value > 100) | value): u8) -> u8 { value + 1 }
fn main() { assert_eq!(next(3), 4); assert_eq!(next(105), 106); }
