// Extracted from library/alloc/src/string.rs:3093
#![allow(unused)]
extern crate alloc;
fn main() {
    let s1: String = String::from("hello world");
    let s2: Box<str> = s1.into_boxed_str();
    let s3: String = String::from(s2);
    
    assert_eq!("hello world", s3)
}
