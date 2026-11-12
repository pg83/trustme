//@ compile-fail: Unknown receiver type
#![feature(arbitrary_self_types)]

trait Pretend { type Target; }
struct Counter;
impl Counter {
    fn invalid(self: &dyn Pretend<Target = Self>) {}
}

fn main() {}
