//@ crate-type: lib
//@ compile-flags: -Z stop-after=typeck

#![allow(dead_code, unused_associated_type_bounds)]

trait Bounds {
    type A where Self: Sized;
    type B where Self: Sized;
}

struct Pair<L: ?Sized, R: ?Sized>(*const L, *const R);

trait Choose {
    fn choose(&self);
}

impl<T: ?Sized> Pair<T, T> {
    fn choose(&self) {}
}

impl<L: ?Sized, R: ?Sized> Choose for Pair<L, R> {
    fn choose(&self) {}
}

fn different_names(pair: &Pair<dyn Bounds<A = u8>, dyn Bounds<B = u8>>) {
    pair.choose();
}

fn different_lengths(pair: &Pair<dyn Bounds, dyn Bounds<A = u8>>) {
    pair.choose();
}
