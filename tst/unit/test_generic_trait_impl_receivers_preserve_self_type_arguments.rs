//@ compile-variant: -O
#![feature(arbitrary_self_types)]
trait Storage { type Pointer; }
trait Read {
    type Item;
    fn read(self: Box<Self>) -> Self::Item;
}
struct Value<T>(T);
impl<T> Storage for Value<T> { type Pointer = Box<Self>; }
impl<T> Read for Value<T> {
    type Item = T;
    fn read(self: <Self as Storage>::Pointer) -> T { self.0 }
}
fn main() {
    assert_eq!(<Value<u8> as Read>::read(Box::new(Value(71))), 71);
    assert_eq!(<Value<u64> as Read>::read(Box::new(Value(73))), 73);
}
