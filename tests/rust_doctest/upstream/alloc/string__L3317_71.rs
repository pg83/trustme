// Extracted from library/alloc/src/string.rs:3317
#![allow(unused)]
#![feature(string_into_chars)]
extern crate alloc;
fn main() {
    
    let mut chars = String::from("abc").into_chars();
    
    assert_eq!(chars.as_str(), "abc");
    chars.next();
    assert_eq!(chars.as_str(), "bc");
    chars.next();
    chars.next();
    assert_eq!(chars.as_str(), "");
}
