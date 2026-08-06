// Extracted from library/alloc/src/string.rs:935
#![allow(unused)]
#![feature(vec_into_raw_parts)]
extern crate alloc;
fn main() {
    let s = String::from("hello");
    
    let (ptr, len, cap) = s.into_raw_parts();
    
    let rebuilt = unsafe { String::from_raw_parts(ptr, len, cap) };
    assert_eq!(rebuilt, "hello");
}
