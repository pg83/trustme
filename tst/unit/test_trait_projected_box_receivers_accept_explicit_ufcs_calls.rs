//@ compile-variant: -O
#![feature(arbitrary_self_types)]
trait Storage { type Pointer; }
trait Read: Storage<Pointer = Box<Self>> {
    fn read(self: <Self as Storage>::Pointer) -> u32;
}
struct Value(u32);
impl Storage for Value { type Pointer = Box<Self>; }
impl Read for Value {
    fn read(self: <Self as Storage>::Pointer) -> u32 { self.0 }
}
fn main() {
    assert_eq!(<Value as Read>::read(Box::new(Value(61))), 61);
    assert_eq!(<Value as Read>::read(Box::new(Value(67))), 67);
}
