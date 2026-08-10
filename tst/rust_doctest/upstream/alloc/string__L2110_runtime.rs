// Extracted from library/alloc/src/string.rs:2110
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = String::from("hello");

    let b = s.into_boxed_str();
}
