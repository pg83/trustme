//@ compile-fail: error:
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]
use std::marker::PhantomData;

struct Pair<A, B>(PhantomData<(A, B)>);
impl<T> Pair<T, T> { type Value = T; }
type Early<T, U> = Pair<ZItem<T>, ZItem<U>>::Value;
type ZItem<T> = T::Item;

fn main() {}
