//@ compile-fail: Value mismatch between
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn width(n: usize, extra: f32) -> usize { n + extra as usize }
fn different<const N: usize>(value: [u8; width(N, 1.25f32)]) -> [u8; width(N, 1.75f32)] { value }
fn main() {}
