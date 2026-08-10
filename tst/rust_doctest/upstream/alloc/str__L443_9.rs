// Extracted from library/alloc/src/str.rs:443
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = "hello";

    assert_eq!("HELLO", s.to_uppercase());
}
