#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

use std::marker::PhantomData;

trait Family {
    type Leaf<const N: usize>;
    type Branch<const N: usize>;
}

struct Source;

impl Family for Source {
    type Leaf<const N: usize> = [u8; N];
    type Branch<const N: usize> = Self::Leaf<N>;
}

struct Holder<T>(PhantomData<T>);

impl<T> Holder<T> {
    type Output = T;
}

type Early = Holder<<Source as Family>::Branch<5>>::Output;

fn main() {
    let value: Early = [7, 11, 13, 17, 19];
    assert_eq!(value, [7u8, 11, 13, 17, 19]);
}
