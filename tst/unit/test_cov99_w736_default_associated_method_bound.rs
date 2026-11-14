//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: error:0:

#![feature(specialization)]
#![allow(incomplete_features)]

trait Family {
    type Item;
}

impl<T> Family for T {
    default type Item = u8;
}

struct Token;
struct Gate<T>(T);

impl<T: Family<Item = u8>> Gate<T> {
    fn enter(&self) {}
}

pub fn trigger() {
    Gate(Token).enter();
}
