// Extracted from library/alloc/src/string.rs:2523
#![allow(unused)]
extern crate alloc;
fn main() {
    assert_eq!(String::from("Hello world").find("world"), Some(6));
}
