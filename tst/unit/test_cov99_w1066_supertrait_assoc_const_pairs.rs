//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

pub trait Consts {
    const A: usize;
    const B: usize;
}

pub trait Parent<const A: usize, const B: usize> {
    type Item;
    fn get(&self) -> Self::Item;
}

pub trait Child<C: Consts>: Parent<{ C::A }, { C::B }>
where
    [(); C::A]:,
    [(); C::B]:,
{}

pub trait Factory<C: Consts>
where
    [(); C::A]:,
    [(); C::B]:,
{
    type Output: Child<C, Item = u8>;
}

pub fn project<C: Consts, F: Factory<C>>(value: &F::Output) -> u8
where
    [(); C::A]:,
    [(); C::B]:,
{
    Parent::<{ C::A }, { C::B }>::get(value)
}
