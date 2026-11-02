//@ compile-fail: Value mismatch between
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn width(n: usize, text: &str) -> usize { n + text.len() }
fn different<const N: usize>(value: [u8; width(N, "elm")]) -> [u8; width(N, "fir")] { value }
fn main() {}
