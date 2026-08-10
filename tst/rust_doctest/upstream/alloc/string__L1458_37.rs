// Extracted from library/alloc/src/string.rs:1458
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::from("hello");

    s.truncate(2);

    assert_eq!("he", s);
}
