// Extracted from library/alloc/src/string.rs:1235
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::new();
    
    s.reserve_exact(10);
    
    assert!(s.capacity() >= 10);
}
