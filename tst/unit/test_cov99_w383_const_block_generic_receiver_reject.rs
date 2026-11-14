//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ compile-fail: Unknown receiver type
#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

struct Key<const N: usize, T>(T);
struct Receiver;
trait Route {
    type Target;
}

impl<const N: usize> Route for Key<N, [u8; const { N + 1 }]> {
    type Target = (Receiver,);
}

impl Receiver {
    fn invalid<const N: usize>(self: <Key<N, [u8; const { N + 1 }]> as Route>::Target) {}
}
