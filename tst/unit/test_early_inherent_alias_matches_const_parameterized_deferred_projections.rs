#![feature(inherent_associated_types)]
#![allow(incomplete_features, type_alias_bounds)]

use std::marker::PhantomData;

trait Family {
    type Left<const N: usize>;
    type Right<const N: usize>;
}

struct Source;

impl Family for Source {
    type Left<const N: usize> = [u8; N];
    type Right<const N: usize> = [u8; N];
}

struct Pair<A, B>(PhantomData<(A, B)>);

impl<T> Pair<T, T> {
    type Output = T;
}

type Selected = Pair<ZLeft<Source>, ZRight<Source>>::Output;
type ZLeft<T: Family> = T::Left<3>;
type ZRight<T: Family> = T::Right<3>;

fn main() {
    let value: Selected = [17, 23, 31];
    assert_eq!(value, [17u8, 23, 31]);
}
