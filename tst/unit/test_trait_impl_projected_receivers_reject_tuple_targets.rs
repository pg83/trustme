//@ compile-fail: Unknown receiver type
#![feature(arbitrary_self_types)]
trait Storage { type Pointer; }
trait Read { fn read(self: Box<Self>) -> u32; }
struct Value;
impl Storage for Value { type Pointer = (Self, u32); }
impl Read for Value {
    fn read(self: <Self as Storage>::Pointer) -> u32 { 0 }
}
fn main() {}
