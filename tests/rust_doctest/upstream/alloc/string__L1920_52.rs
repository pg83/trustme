// Extracted from library/alloc/src/string.rs:1920
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::from("foo");
    
    s.clear();
    
    assert!(s.is_empty());
    assert_eq!(0, s.len());
    assert_eq!(3, s.capacity());
}
