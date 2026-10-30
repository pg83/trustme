//@ compile-fail: Value mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
struct Packet<const N: usize>([u8; N]);
const fn width(n: usize, step: usize) -> usize { n + step }
fn change<const N: usize>(value: Packet<{width(N, { 8; 2 })}>) -> Packet<{width(N, { 8; 3 })}> { value }
fn main() {}
