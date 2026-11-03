//@ compile-fail: error:
//@ aux-build: destination_unsizing.rs
extern crate destination_unsizing as fixture;
fn main() {
    let source: fixture::Tuple<[u16; 3], u32> = fixture::Tuple(&fixture::DATA, &fixture::NUMBER);
    let both: fixture::Tuple<[u16], dyn fixture::Value> = source;
    assert_eq!(unsafe { &*both.0 }.len(), 3);
}
