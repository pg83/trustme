//@ crate-type: lib
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

pub trait Extent {
    const N: usize;
}

pub trait Marker<const N: usize> {}

pub fn repeat<T: Extent, U>()
where
    U: Marker<{ T::N }> + Marker<{ T::N }>,
{
}
