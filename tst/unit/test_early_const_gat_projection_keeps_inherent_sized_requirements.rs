//@ compile-fail: error:
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

use std::marker::PhantomData;

trait Family {
    type Leaf<const N: usize>: ?Sized;
    type Branch<const N: usize>: ?Sized;
}

struct Source;

impl Family for Source {
    type Leaf<const N: usize> = str;
    type Branch<const N: usize> = Self::Leaf<N>;
}

struct Holder<T: ?Sized>(PhantomData<T>);

impl<T> Holder<T> {
    type Output = u8;
}

type Early = Holder<<Source as Family>::Branch<5>>::Output;

fn main() {
    let _: Early = 7;
}
