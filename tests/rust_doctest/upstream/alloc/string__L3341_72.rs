// Extracted from library/alloc/src/string.rs:3341
#![allow(unused)]
#![feature(string_into_chars)]
extern crate alloc;
fn main() {
    
    let chars = String::from("abc").into_chars();
    assert_eq!(chars.into_string(), "abc");
    
    let mut chars = String::from("def").into_chars();
    chars.next();
    assert_eq!(chars.into_string(), "ef");
}
