//@ compile-variant: -O
#![feature(arbitrary_self_types)]
trait Storage { type Stored: ?Sized; }
trait Read: Storage<Stored = Self> {
    fn read(self: &<Self as Storage>::Stored) -> u32;
}
struct Value(u32);
impl Storage for Value { type Stored = Self; }
impl Read for Value {
    fn read(&self) -> u32 { self.0 }
}
fn main() { assert_eq!(<Value as Read>::read(&Value(83)), 83); }
