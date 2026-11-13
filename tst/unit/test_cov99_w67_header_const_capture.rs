//@ crate-type: lib
#![feature(generic_const_exprs, inherent_associated_types)]
#![allow(incomplete_features, dead_code)]

trait Source {
    type Item;
}

struct Pair<T, U>(T, U);
struct Output;

impl Output {
    type Value = u8;
}

trait Select<const N: usize> {
    type Result;
}

impl<T: Source, const N: usize> Select<N> for Pair<T, T::Item> {
    type Result = Output;
}

impl<T: Source> Pair<T, T::Item> {
    fn probe(_: <<Self as Select<{ core::mem::size_of::<T>() }>>::Result>::Value)
    where
        [(); core::mem::size_of::<T>()]:,
    {
    }
}
