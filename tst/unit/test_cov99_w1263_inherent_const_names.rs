//@ compile-fail: Value mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Widths<const N: usize>;

impl<const N: usize> Widths<N> {
    const HEAD: usize = N;
    const TAIL: usize = N + 1;
}

fn different<const N: usize>(value: [u8; Widths::<N>::HEAD]) -> [u8; Widths::<N>::TAIL]
where
    [u8; Widths::<N>::HEAD]:,
    [u8; Widths::<N>::TAIL]:,
{
    value
}

fn main() {}
