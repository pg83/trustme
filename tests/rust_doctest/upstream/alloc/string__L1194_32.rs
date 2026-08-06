// Extracted from library/alloc/src/string.rs:1194
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut s = String::with_capacity(10);
    s.push('a');
    s.push('b');
    
    // s now has a length of 2 and a capacity of at least 10
    let capacity = s.capacity();
    assert_eq!(2, s.len());
    assert!(capacity >= 10);
    
    // Since we already have at least an extra 8 capacity, calling this...
    s.reserve(8);
    
    // ... doesn't actually increase.
    assert_eq!(capacity, s.capacity());
}
