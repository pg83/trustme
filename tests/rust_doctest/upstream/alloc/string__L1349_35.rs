// Extracted from library/alloc/src/string.rs:1349
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::from("foo");
    
    s.reserve(100);
    assert!(s.capacity() >= 100);
    
    s.shrink_to_fit();
    assert_eq!(3, s.capacity());
}
