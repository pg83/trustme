// Extracted from library/alloc/src/str.rs:492
#![allow(unused)]
extern crate alloc;
fn main() {
    let string = String::from("birthday gift");
    let boxed_str = string.clone().into_boxed_str();

    assert_eq!(boxed_str.into_string(), string);
}
