// Two trait obligations on one type are the same only if their associated
// type bounds are: `Range<{integer}>: Iterator<Item = A>` says what `A` is,
// `Range<{integer}>: Iterator` does not. The ordering of the bound maps
// treated a map that is a prefix of another as equal, so the first was
// dropped as a duplicate of the second (registered earlier by the blanket
// `impl<I: Iterator> Ext for I`), `A` never met `{integer}`, and `data[a]`
// failed with "type annotations needed" - itertools' iris example, through
// `(0..4).tuple_combinations()`.
use std::marker::PhantomData;

pub trait HasPair<I>: Sized {}
impl<A, I: Iterator<Item = A>> HasPair<I> for (A, A) {}

pub struct Pairs<I, T>(I, PhantomData<T>);
impl<I: Iterator, T: HasPair<I>> Iterator for Pairs<I, T> {
    type Item = T;
    fn next(&mut self) -> Option<T> {
        None
    }
}

pub trait Ext: Iterator + Sized {
    fn pairs<T>(self) -> Pairs<Self, T>
    where
        T: HasPair<Self>,
    {
        Pairs(self, PhantomData)
    }
}
impl<I: Iterator> Ext for I {}

fn main() {
    let data = [1.0f32, 2.0, 3.0, 4.0];
    let mut seen = 0.0;
    for (a, b) in (0..4).pairs() {
        seen += data[a] + data[b];
    }
    assert_eq!(seen, 0.0);
    assert_eq!(data[(0..4).len() - 1], 4.0);
}
