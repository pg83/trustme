//@ crate-type: lib
//@ compile-flags: --emit=metadata
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

fn accept<const N: usize>(_: [u8; N + 1])
where
    [(); N + 1]:,
{}

pub fn nested<const M: usize>(input: [u8; (M + 1) + 1])
where
    [(); M + 1]:,
    [(); (M + 1) + 1]:,
{
    accept::<{ M + 1 }>(input);
}
