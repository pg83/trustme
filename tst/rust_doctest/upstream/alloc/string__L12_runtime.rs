// Extracted from library/alloc/src/string.rs:12
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = "Hello".to_string();

    let s = String::from("world");
    let s: String = "also this".into();
}
