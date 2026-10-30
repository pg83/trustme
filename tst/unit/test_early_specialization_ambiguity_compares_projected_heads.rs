//@ compile-fail: type annotations needed
#![feature(specialization, inherent_associated_types)]
#![allow(incomplete_features)]

use std::marker::PhantomData;

trait Family {
    type Item;
    type Other;
}

impl Family for u8 {
    type Item = u16;
    type Other = u16;
}

struct Group<A, B>(PhantomData<(A, B)>);

trait Rank<A> {
    const LENGTH: usize;
}

impl<T: Family, U> Rank<T::Item> for Group<T, U> {
    default const LENGTH: usize = 3;
}

impl<T: Family<Item = <T as Family>::Other>> Rank<T::Other> for Group<T, T::Other> {
    const LENGTH: usize = 5;
}

impl Rank<u32> for Group<u8, u16> {
    const LENGTH: usize = 5;
}

trait Length {}
impl Length for [u8; 5] {}

struct Holder<T>(T);
impl<T: Length> Holder<T> {
    type Array = [u8; 17];
}

type Selected = Holder<[u8; Group::<u8, u16>::LENGTH]>::Array;

fn main() {}
