//@ compile-fail: Value mismatch between
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn width(n: usize, extra: bool) -> usize { n + extra as usize }
fn different<const N: usize>(value: [u8; width(N, true)]) -> [u8; width(N, false)] { value }
fn main() {}
