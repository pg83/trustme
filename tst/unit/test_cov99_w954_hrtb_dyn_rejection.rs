//@ crate-type: lib
//@ compile-flags: --emit=dep-info -Z dump-hir
//@ compile-fail: not dyn compatible
#![feature(non_lifetime_binders)]
#![allow(incomplete_features)]

trait Parent<T> {
    type Item;
}

trait Child: for<T> Parent<T, Item = ()> {}

pub struct Carrier<T: ?Sized>(*const T);

impl Carrier<dyn Child<Item = ()>> {
    pub fn witness() {}
}
