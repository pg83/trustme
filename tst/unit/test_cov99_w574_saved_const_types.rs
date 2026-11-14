//@ crate-type: lib
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

pub trait Width {
    const N: usize;
}

impl<T> Width for T {
    const N: usize = {
        let marker: *const T = 0 as *const T;
        let _ = marker;
        core::mem::size_of::<T>()
    };
}

pub fn make<T>() -> impl Fn([T; <T as Width>::N]) -> usize
where
    [(); <T as Width>::N]:,
{
    |items| items.len()
}
