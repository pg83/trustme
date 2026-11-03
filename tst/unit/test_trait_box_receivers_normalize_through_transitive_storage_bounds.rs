//@ compile-variant: -O
#![feature(arbitrary_self_types)]
trait Storage { type Pointer; }
trait Boxed: Storage<Pointer = Box<Self>> {}
trait Read: Boxed {
    fn read(self: <Self as Storage>::Pointer) -> u32;
}
struct Value(u32);
impl Storage for Value { type Pointer = Box<Self>; }
impl Boxed for Value {}
impl Read for Value {
    fn read(self: Box<Self>) -> u32 { self.0 }
}
fn main() { assert_eq!(<Value as Read>::read(Box::new(Value(79))), 79); }
