//@ aux-build: projected_receiver_exports.rs
//@ compile-variant: -O
use projected_receiver_exports::{Read, Storage};
struct Value(u32);
impl Storage for Value { type Pointer = Box<Self>; }
impl Read for Value {
    fn read(self: Box<Self>) -> u32 { self.0 }
}
fn main() { assert_eq!(<Value as Read>::read(Box::new(Value(101))), 101); }
