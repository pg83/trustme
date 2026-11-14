//@ compile-only
//@ compile-flags: --emit=metadata
#![feature(arbitrary_self_types)]
#![allow(dead_code)]

use std::ops::Deref;

trait ReceiverSet {
    type Pointer: Deref<Target = Self>;
    fn projected(self: Self::Pointer);
    fn generic<R: Deref<Target = Self>>(self: R);
}

fn main() {}
