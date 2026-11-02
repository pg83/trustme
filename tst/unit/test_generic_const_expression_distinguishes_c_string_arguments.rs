//@ compile-fail: Value mismatch between
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn width(n: usize, text: &core::ffi::CStr) -> usize { n + text.to_bytes().len() }
fn different<const N: usize>(value: [u8; width(N, c"fir")]) -> [u8; width(N, c"elm")] { value }
fn main() {}
