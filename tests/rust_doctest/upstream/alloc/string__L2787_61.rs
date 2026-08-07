// Extracted from library/alloc/src/string.rs:2787
#![allow(unused)]
extern crate alloc;
fn main() {
    let i = 5;
    let five = String::from("5");

    assert_eq!(five, i.to_string());
}
