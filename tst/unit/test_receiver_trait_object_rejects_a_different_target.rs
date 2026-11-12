//@ compile-fail: Unknown receiver type
#![feature(arbitrary_self_types)]

use std::ops::Receiver;

struct Counter;
impl Counter {
    fn invalid(self: &dyn Receiver<Target = (Self,)>) {}
}

fn main() {}
