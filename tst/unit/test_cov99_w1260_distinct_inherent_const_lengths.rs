//@ compile-fail: mismatch
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct One;
struct Two;

impl One {
    const fn width<const N: usize>() -> usize {
        N
    }
}

impl Two {
    const fn width<const N: usize, const M: usize>() -> usize {
        N + M
    }
}

fn reject<const N: usize>(value: [u8; One::width::<N>()]) -> [u8; Two::width::<N, 1>()]
where
    [(); One::width::<N>()]: Sized,
    [(); Two::width::<N, 1>()]: Sized,
{
    value
}

fn main() {}
