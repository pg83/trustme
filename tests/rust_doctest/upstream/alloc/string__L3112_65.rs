// Extracted from library/alloc/src/string.rs:3112
#![allow(unused)]
extern crate alloc;
fn main() {
    let s1: String = String::from("hello world");
    let s2: Box<str> = Box::from(s1);
    let s3: String = String::from(s2);
    
    assert_eq!("hello world", s3)
}
