// Extracted from library/alloc/src/string.rs:1184
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::new();
    
    s.reserve(10);
    
    assert!(s.capacity() >= 10);
}
