//@ compile-fail: Value mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const fn width<const N: usize>() -> usize { N + 1 }

fn change<const N: usize, const M: usize>(value: [u8; width::<N>()]) -> [u8; width::<M>()]
where
    [(); width::<N>()]:,
    [(); width::<M>()]:,
{
    value
}

fn main() {}
