// Extracted from library/alloc/src/str.rs:228
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = "this is a string";
    let boxed_str = s.to_owned().into_boxed_str();
    let boxed_bytes = boxed_str.into_boxed_bytes();
    assert_eq!(*boxed_bytes, *s.as_bytes());
}
