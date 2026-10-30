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

struct Owner<T, const N: usize>(PhantomData<T>);

impl<T: Count> Owner<T, { T::N }>
where
    [(); T::N]:,
    [(); T::N + 1]:,
{
    const fn values() -> &'static [u8] {
        &[T::VALUE; T::N + 1]
    }
}

const FIRST: &[u8] = Owner::<u8, 3>::values();
const SECOND: &[u8] = Owner::<u16, 4>::values();

fn main() {
    assert_eq!(FIRST, [5; 4]);
    assert_eq!(SECOND, [9; 5]);
}
