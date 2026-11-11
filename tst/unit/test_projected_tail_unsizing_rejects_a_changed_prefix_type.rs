//@ compile-fail: Type mismatch between u32 and u16
#![feature(unsize)]

use std::marker::{PhantomData, Unsize};

trait Tail {
    type Item: ?Sized;
}

struct Identity<T: ?Sized>(PhantomData<T>);

impl<T: ?Sized> Tail for Identity<T> {
    type Item = T;
}

struct Packet<H, T: Tail> {
    header: H,
    payload: T::Item,
}

fn require_unsize<S: ?Sized + Unsize<D>, D: ?Sized>() {}

fn main() {
    require_unsize::<Packet<u16, Identity<[u8; 3]>>, Packet<u32, Identity<[u8]>>>();
}
