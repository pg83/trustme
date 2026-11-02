//@ compile-fail: Value mismatch between
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn width(n: usize, text: &[u8; 3]) -> usize { n + text.len() }
fn different<const N: usize>(value: [u8; width(N, b"oak")]) -> [u8; width(N, b"ash")] { value }
fn main() {}
