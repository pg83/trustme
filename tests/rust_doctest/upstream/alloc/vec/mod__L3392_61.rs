// Extracted from library/alloc/src/vec/mod.rs:3392
#![allow(unused)]
extern crate alloc;
fn main() {
    let mut vec = vec![1, 2, 2, 3, 2];
    
    vec.dedup();
    
    assert_eq!(vec, [1, 2, 3, 2]);
}
