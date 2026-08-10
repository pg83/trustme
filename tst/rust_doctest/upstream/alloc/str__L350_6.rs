// Extracted from library/alloc/src/str.rs:350
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = "HELLO";

    assert_eq!("hello", s.to_lowercase());
}
