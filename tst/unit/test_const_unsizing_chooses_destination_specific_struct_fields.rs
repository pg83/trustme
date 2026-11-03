//@ aux-build: destination_unsizing.rs
extern crate destination_unsizing as fixture;
const FIRST: fixture::Tuple<[u16], u32> = { let source: fixture::Tuple<[u16; 3], u32> = fixture::Tuple(&fixture::DATA, &fixture::NUMBER); source };
const SECOND: fixture::Tuple<[u16; 3], dyn fixture::Value> = { let source: fixture::Tuple<[u16; 3], u32> = fixture::Tuple(&fixture::DATA, &fixture::NUMBER); source };
fn main() {
    assert_eq!(unsafe { &*FIRST.0 }.len(), 3); assert_eq!(unsafe { *FIRST.1 }, 229);
    assert_eq!(unsafe { (*SECOND.0)[0] }, 211); assert_eq!(unsafe { &*SECOND.1 }.value(), 229);
}
