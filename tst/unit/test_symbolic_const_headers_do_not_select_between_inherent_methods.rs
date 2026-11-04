//@ compile-fail: error
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

use std::marker::PhantomData;

struct Holder<T, const A: usize, const B: usize>(PhantomData<T>);

impl<const N: usize> Holder<u8, { N + 1 }, N>
where [(); N + 1]: {
    fn measure(&self) -> usize { 43 }
}

impl<const N: usize> Holder<u16, { N + 1 }, N>
where [(); N + 1]: {
    fn measure(&self) -> usize { 47 }
}

fn unresolved<const N: usize, const M: usize>()
where [(); N + 1]:, [(); M + 1]: {
    let value = Holder::<_, { M + 1 }, N>(PhantomData);
    let _ = value.measure();
}

fn main() { unresolved::<3, 3>(); }
