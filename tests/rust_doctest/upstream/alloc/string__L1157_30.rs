// Extracted from library/alloc/src/string.rs:1157
#![allow(unused)]
extern crate alloc;
fn main() {
    let s = String::with_capacity(10);
    
    assert!(s.capacity() >= 10);
}
