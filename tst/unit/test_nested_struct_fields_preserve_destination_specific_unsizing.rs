//@ aux-build: destination_unsizing.rs
#![feature(coerce_unsized)]
extern crate destination_unsizing as fixture;
use std::ops::CoerceUnsized;
struct Envelope<A: ?Sized, B: ?Sized>(fixture::Tuple<A, B>);
impl CoerceUnsized<Envelope<[u16], u32>> for Envelope<[u16; 3], u32> {}
impl CoerceUnsized<Envelope<[u16; 3], dyn fixture::Value>> for Envelope<[u16; 3], u32> {}
fn main() {
    let source: Envelope<[u16; 3], u32> = Envelope(fixture::Tuple(&fixture::DATA, &fixture::NUMBER));
    let first: Envelope<[u16], u32> = source;
    assert_eq!(unsafe { &*first.0.0 }, &[211, 223, 227]);
    let source: Envelope<[u16; 3], u32> = Envelope(fixture::Tuple(&fixture::DATA, &fixture::NUMBER));
    let second: Envelope<[u16; 3], dyn fixture::Value> = source;
    assert_eq!(unsafe { &*second.0.1 }.value(), 229);
}
