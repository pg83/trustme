//@ compile-fail: Type mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]
const fn count(n: usize) -> usize { n + 1 }
struct Buffer<const N: usize>([u8; N]);
fn read<const N: usize>(value: Buffer<{ count(N) }>) -> u8 where [(); count(N)]: { value.0[0] }
fn choose<const N: usize, const M: usize>() -> fn(Buffer<{ count(M) }>) -> u8
where [(); count(N)]:, [(); count(M)]: { read::<N> }
fn main() {}
