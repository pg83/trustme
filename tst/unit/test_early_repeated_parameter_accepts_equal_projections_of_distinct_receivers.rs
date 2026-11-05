//@ compile-variant: -O
#![feature(inherent_associated_types)]
#![allow(incomplete_features, type_alias_bounds)]
use std::marker::PhantomData;

struct Pair<A, B>(PhantomData<(A, B)>);
impl<T> Pair<T, T> { type Value = T; }
trait Provider { type Item; }
struct First;
struct Second;
impl Provider for First { type Item = u16; }
impl Provider for Second { type Item = u16; }

type Early = Pair<ZItem<First>, ZItem<Second>>::Value;
type ZItem<T: Provider> = T::Item;

fn main() {
    let value: Early = 43;
    assert_eq!(value, 43u16);
}
