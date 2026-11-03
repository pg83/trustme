//@ compile-fail: Unknown receiver type
#![feature(arbitrary_self_types)]
trait Storage { type Pointer; }
trait Invalid: Storage<Pointer = (Self, u32)> + Sized {
    fn invalid(self: <Self as Storage>::Pointer);
}
fn main() {}
