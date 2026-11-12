//@ compile-fail: Unknown receiver type
#![feature(arbitrary_self_types)]

use std::ops::Receiver;

struct Counter;
trait Carrier {
    type Handle: Receiver<Target = (Counter,)>;
}
impl Counter {
    fn invalid<C: Carrier>(self: <C as Carrier>::Handle) {}
}

fn main() {}
