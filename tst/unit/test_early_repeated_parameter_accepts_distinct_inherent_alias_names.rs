//@ compile-variant: -O
#![feature(inherent_associated_types)]
#![allow(incomplete_features)]
use std::marker::PhantomData;

struct Pair<A, B>(PhantomData<(A, B)>);
impl<T> Pair<T, T> { type Value = T; }
struct Source;
impl Source { type Left = u16; type Right = u16; }

type Early = Pair<ZLeft, ZRight>::Value;
type ZLeft = Source::Left;
type ZRight = Source::Right;

fn main() {
    let value: Early = 41;
    assert_eq!(value, 41u16);
}
