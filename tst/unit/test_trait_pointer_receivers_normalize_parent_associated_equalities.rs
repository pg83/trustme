//@ compile-variant: -O
#![feature(arbitrary_self_types, arbitrary_self_types_pointers)]
trait Storage { type Stored: ?Sized; }
trait Read: Storage<Stored = Self> {
    unsafe fn read(self: *const <Self as Storage>::Stored) -> u32;
}
struct Value(u32);
impl Storage for Value { type Stored = Self; }
impl Read for Value {
    unsafe fn read(self: *const Self) -> u32 { (*self).0 }
}
fn main() {
    let value = Value(89);
    assert_eq!(unsafe { <Value as Read>::read(&value as *const Value) }, 89);
}
