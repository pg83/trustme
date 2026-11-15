//@ compile-fail: cycle detected when evaluating constant
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

const CYCLE: usize = cycle::<0>();

const fn cycle<const N: usize>() -> usize
where
    [(); 0 | N]: Sized,
{
    let _: [u8; 0 | N] = [0u8; CYCLE | N];
    0
}

fn main() {}
