//@ compile-variant: -O
#![feature(inherent_associated_types)]
#![allow(incomplete_features, type_alias_bounds)]
use std::marker::PhantomData;

struct Pair<A, B>(PhantomData<(A, B)>);
impl<T> Pair<T, T> { type Value = T; }
trait Provider { type Left; type Right; }
struct Source;
impl Provider for Source { type Left = u16; type Right = u16; }

type Early = Pair<ZLeft<Source>, ZRight<Source>>::Value;
type ZLeft<T: Provider> = T::Left;
type ZRight<T: Provider> = T::Right;

fn main() {
    let value: Early = 31;
    assert_eq!(value, 31u16);
}
