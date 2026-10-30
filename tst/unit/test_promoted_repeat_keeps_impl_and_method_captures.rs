#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

use std::marker::PhantomData;

trait Count {
    const N: usize;
    const VALUE: u8;
}

impl Count for u8 {
    const N: usize = 3;
    const VALUE: u8 = 5;
}

impl Count for u16 {
    const N: usize = 4;
    const VALUE: u8 = 9;
}

struct Owner<T, const BASE: usize>(PhantomData<T>);

impl<T: Count, const BASE: usize> Owner<T, BASE> {
    const fn values<const EXTRA: usize>() -> &'static [u8]
    where
        [(); T::N + BASE + EXTRA]:,
    {
        &[T::VALUE; T::N + BASE + EXTRA]
    }
}

const FIRST: &[u8] = Owner::<u8, 2>::values::<3>();
const SECOND: &[u8] = Owner::<u16, 1>::values::<2>();

fn main() {
    assert_eq!(FIRST, [5; 8]);
    assert_eq!(SECOND, [9; 7]);
}
