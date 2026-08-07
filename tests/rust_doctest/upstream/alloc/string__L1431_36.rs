// Extracted from library/alloc/src/string.rs:1431
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = String::from("hello");

    assert_eq!(&[104, 101, 108, 108, 111], s.as_bytes());
}
