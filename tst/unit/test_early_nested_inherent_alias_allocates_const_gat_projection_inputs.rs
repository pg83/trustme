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
    type Branch<const N: usize> = Holder<Self::Leaf<N>>;
}

struct Holder<T>(PhantomData<T>);

impl<T> Holder<T> {
    type Output = T;
}

struct Outer<T>(PhantomData<T>);

impl<T> Outer<T> {
    type Output = T;
}

type Early = Outer<<<Source as Family>::Branch<5>>::Output>::Output;

fn main() {
    let value: Early = [2, 3, 5, 7, 11];
    assert_eq!(value, [2u8, 3, 5, 7, 11]);
}
