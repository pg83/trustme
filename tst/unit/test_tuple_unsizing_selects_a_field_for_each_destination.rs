//@ compile-variant: -O
//@ aux-build: destination_unsizing.rs
extern crate destination_unsizing as fixture;
fn main() {
    let source: fixture::Tuple<[u16; 3], u32> = std::hint::black_box(fixture::Tuple(&fixture::DATA, &fixture::NUMBER));
    let slice: fixture::Tuple<[u16], u32> = source;
    assert_eq!(unsafe { &*slice.0 }, &[211, 223, 227]);
    assert_eq!(unsafe { *slice.1 }, 229);
    let source: fixture::Tuple<[u16; 3], u32> = std::hint::black_box(fixture::Tuple(&fixture::DATA, &fixture::NUMBER));
    let dynamic: fixture::Tuple<[u16; 3], dyn fixture::Value> = source;
    assert_eq!(unsafe { (*dynamic.0)[2] }, 227);
    assert_eq!(unsafe { &*dynamic.1 }.value(), 229);
}
