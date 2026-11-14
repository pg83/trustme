//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

#![feature(unsize)]

use core::marker::Unsize;

struct Tail<T: ?Sized>(T);

fn require_unsize<S: ?Sized + Unsize<D>, D: ?Sized>() {}

pub fn declared_tail<S: ?Sized + Unsize<D>, D: ?Sized>() {
    require_unsize::<Tail<S>, Tail<D>>();
}
