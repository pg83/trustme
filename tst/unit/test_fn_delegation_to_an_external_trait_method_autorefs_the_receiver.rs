/* rustc `finalize_body_lowering`: a delegation to a trait method is a method call
   on the body's value, whichever crate declares the trait, so `self.inner` autorefs
   for `Iterator::next(&mut self)`.  Passing it as a plain argument met `&mut Self`
   with the `Map` value itself. */
#![feature(fn_delegation)]
#![allow(incomplete_features)]
use std::iter::{Iterator, Map};

pub struct MapOuter<I, F> {
    pub inner: Map<I, F>,
}

impl<B, I: Iterator, F> Iterator for MapOuter<I, F>
where
    F: FnMut(I::Item) -> B,
{
    type Item = <Map<I, F> as Iterator>::Item;

    reuse Iterator::{next, fold} { self.inner }
}

fn main() {
    let x = vec![1, 2, 3];
    let iter = x.iter().map(|val| val * 2);
    let outer_iter = MapOuter { inner: iter };
    let val = outer_iter.fold(0, |acc, x| acc + x);
    assert_eq!(val, 12);
}
