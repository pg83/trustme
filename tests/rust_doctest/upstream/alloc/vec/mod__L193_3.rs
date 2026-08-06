// Extracted from library/alloc/src/vec/mod.rs:193
#![allow(unused)]
extern crate alloc;
fn main() {
    let vec = vec![0; 5];
    assert_eq!(vec, [0, 0, 0, 0, 0]);
    
    // The following is equivalent, but potentially slower:
    let mut vec = Vec::with_capacity(5);
    vec.resize(5, 0);
    assert_eq!(vec, [0, 0, 0, 0, 0]);
}
