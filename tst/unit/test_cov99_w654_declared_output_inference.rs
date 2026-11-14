//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

#![feature(inherent_associated_types)]

use core::future::Future;

trait Identity {
    type Item;
}

struct U16;

impl Identity for U16 {
    type Item = u16;
}

struct Pair<T>(T);

impl<T> Pair<(T, T)> {
    type Out = T;
}

fn projected<A: Identity<Item = B>, B>() -> Option<Pair<(B, A::Item)>::Out> {
    None
}

fn require_u16<F: Future<Output = Option<u16>>>(_: F) {}

fn main() {
    let future = async { projected::<U16, _>() };
    require_u16(future);
}
