//@ aux-build: destination_unsizing.rs
#![feature(coerce_unsized)]
extern crate destination_unsizing as fixture;
use std::ops::CoerceUnsized;
struct Outer<T>(T);
impl<T: CoerceUnsized<U>, U> CoerceUnsized<Outer<U>> for Outer<T> {}
fn main() {
    let source: Outer<fixture::Tuple<[u16; 3], u32>> = Outer(fixture::Tuple(&fixture::DATA, &fixture::NUMBER));
    let slice: Outer<fixture::Tuple<[u16], u32>> = source;
    assert_eq!(unsafe { &*slice.0.0 }.len(), 3);
    let source: Outer<fixture::Tuple<[u16; 3], u32>> = Outer(fixture::Tuple(&fixture::DATA, &fixture::NUMBER));
    let dynamic: Outer<fixture::Tuple<[u16; 3], dyn fixture::Value>> = source;
    assert_eq!(unsafe { &*dynamic.0.1 }.value(), 229);
}
