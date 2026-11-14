//@ compile-only
//@ compile-flags: --emit=metadata -Cpanic=abort

#![feature(inherent_associated_types)]

use core::future::Future;

struct Pair<T>(T);

impl<T> Pair<(T, T)> {
    type Out = T;
}

fn require_u16<F: Future<Output = Option<u16>>>(_: F) {}

fn main() {
    let future = async {
        let value: Option<Pair<(_, u16)>::Out> = None;
        value
    };
    require_u16(future);
}
