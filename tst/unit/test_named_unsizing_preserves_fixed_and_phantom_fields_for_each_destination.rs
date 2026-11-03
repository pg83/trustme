//@ aux-build: destination_unsizing.rs
extern crate destination_unsizing as fixture;
use std::marker::PhantomData;
fn main() {
    let source: fixture::Named<[u16; 3], u32> = std::hint::black_box(fixture::Named { tag: 233, first: &fixture::DATA, marker: PhantomData, second: &fixture::NUMBER });
    let slice: fixture::Named<[u16], u32> = source;
    assert_eq!(slice.tag, 233); assert_eq!(unsafe { &*slice.first }, &[211, 223, 227]);
    let source: fixture::Named<[u16; 3], u32> = std::hint::black_box(fixture::Named { tag: 239, first: &fixture::DATA, marker: PhantomData, second: &fixture::NUMBER });
    let dynamic: fixture::Named<[u16; 3], dyn fixture::Value> = source;
    assert_eq!(dynamic.tag, 239); assert_eq!(unsafe { &*dynamic.second }.value(), 229);
}
