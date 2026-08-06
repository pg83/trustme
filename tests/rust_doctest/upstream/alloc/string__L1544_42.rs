// Extracted from library/alloc/src/string.rs:1544
#![allow(unused)]
#![feature(string_remove_matches)]
extern crate alloc;
fn main() {
    let mut s = String::from("Trees are not green, the sky is not blue.");
    s.remove_matches("not ");
    assert_eq!("Trees are green, the sky is blue.", s);
}
