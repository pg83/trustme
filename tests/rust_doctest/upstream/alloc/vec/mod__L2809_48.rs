// Extracted from library/alloc/src/vec/mod.rs:2809
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut v = vec![1, 2, 3];
    
    v.clear();
    
    assert!(v.is_empty());
}
