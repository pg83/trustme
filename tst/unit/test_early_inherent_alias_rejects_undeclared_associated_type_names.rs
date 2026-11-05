//@ compile-fail: error:
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]
use std::marker::PhantomData;

struct Pair<A, B>(PhantomData<(A, B)>);
impl<T> Pair<T, T> { type Value = T; }
type Early<T> = Pair<ZLeft<T>, ZRight<T>>::Value;
type ZLeft<T> = T::Left;
type ZRight<T> = T::Right;

fn main() {}
