//@ compile-variant: -O
#![feature(arbitrary_self_types)]
trait Storage { type Pointer; }
trait Read { fn read(self: Box<Self>) -> u32; }
struct Value(u32);
impl Storage for Value { type Pointer = Box<Self>; }
impl Read for Value {
    fn read(self: <Self as Storage>::Pointer) -> u32 { self.0 }
}
fn main() {
    assert_eq!(<Value as Read>::read(Box::new(Value(43))), 43);
    assert_eq!(Box::new(Value(47)).read(), 47);
}
