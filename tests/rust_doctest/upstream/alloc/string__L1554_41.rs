// Extracted from library/alloc/src/string.rs:1554
#![allow(unused)]
#![feature(string_remove_matches)]
extern crate alloc;
fn main() {
    let mut s = String::from("banana");
    s.remove_matches("ana");
    assert_eq!("bna", s);
}
