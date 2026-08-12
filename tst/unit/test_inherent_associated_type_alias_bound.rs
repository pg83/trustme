#![feature(inherent_associated_types)]
#![allow(incomplete_features)]

trait Bound {}
struct Source<T>(T);

impl<T: Bound> Source<T> {
    type Item = ();
}

type Alias<T: Bound> = Source<T>::Item;

fn main() {}
