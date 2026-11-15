//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

pub struct Mark<const N: usize>;

pub trait Family<const N: usize> {
    type Item;
}

pub struct Factory;

impl<const N: usize> Family<N> for Factory {
    type Item = Mark<N>;
}

type Via<const N: usize> = <Factory as Family<N>>::Item;

pub fn symbolic_alias<const N: usize>(value: Mark<{ N + 1 }>) -> Mark<{ N + 1 }>
where
    [(); N + 1]:,
{
    let projected: Via<_> = value;
    projected
}
