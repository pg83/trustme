//@ compile-fail: Failed to infer type
trait Read<T> { fn value(&self) -> u8; }
trait Repeat<T>: Read<T> { fn again(&self) -> u8; }
impl<T> Read<T> for u8 { fn value(&self) -> u8 { *self } }
impl<T> Repeat<T> for u8 { fn again(&self) -> u8 { self.value() } }
fn main() { assert_eq!(<u8 as Repeat<u16>>::again(&7), 7); }
