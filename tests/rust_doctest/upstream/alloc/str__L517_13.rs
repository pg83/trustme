// Extracted from library/alloc/src/str.rs:517
#![allow(unused)]
extern crate alloc;
fn main() {
    assert_eq!("abc".repeat(4), String::from("abcabcabcabc"));
}
