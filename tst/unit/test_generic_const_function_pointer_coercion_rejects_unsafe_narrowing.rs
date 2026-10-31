//@ compile-fail: Type mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn count(n: usize) -> usize { n + 1 }
struct Buffer<const N: usize>([u8; N]);

fn narrow<const N: usize>(value: unsafe fn(Buffer<{ count(N) }>) -> u8)
    -> fn(Buffer<{ count(N) }>) -> u8
where [(); count(N)]: { value }

fn main() {}
