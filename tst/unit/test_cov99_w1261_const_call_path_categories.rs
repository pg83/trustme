//@ compile-fail: mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn plain<const N: usize>() -> usize {
    N
}

struct Extent<const N: usize>;

impl<const N: usize> Extent<N> {
    const fn associated() -> usize {
        N + 1
    }
}

fn incompatible<const N: usize>(value: [u8; plain::<N>()]) -> [u8; Extent::<N>::associated()]
where
    [(); plain::<N>()]:,
    [(); Extent::<N>::associated()]:,
{
    value
}

fn main() {}
