//@ compile-fail: Value mismatch between
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
fn different<const N: usize>(value: [u8; N + (true as usize)]) -> [u8; N + (1u8 as usize)] { value }
fn main() {}
