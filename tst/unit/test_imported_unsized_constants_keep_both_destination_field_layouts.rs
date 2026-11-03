//@ aux-build: destination_unsizing.rs
extern crate destination_unsizing as fixture;
fn main() {
    assert_eq!(unsafe { &*fixture::SLICE.0 }, &[211, 223, 227]);
    assert_eq!(unsafe { *fixture::SLICE.1 }, 229);
    assert_eq!(unsafe { (*fixture::DYNAMIC.0)[1] }, 223);
    assert_eq!(unsafe { &*fixture::DYNAMIC.1 }.value(), 229);
}
