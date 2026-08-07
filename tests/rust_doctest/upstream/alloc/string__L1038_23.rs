// Extracted from library/alloc/src/string.rs:1038
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = String::from("hello");
    let bytes = s.into_bytes();

    assert_eq!(&[104, 101, 108, 108, 111][..], &bytes[..]);
}
