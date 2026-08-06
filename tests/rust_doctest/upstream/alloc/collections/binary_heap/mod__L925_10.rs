// Extracted from library/alloc/src/collections/binary_heap/mod.rs:925
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    
    let mut a = BinaryHeap::from([-10, 1, 2, 3, 3]);
    let mut b = BinaryHeap::from([-20, 5, 43]);
    
    a.append(&mut b);
    
    assert_eq!(a.into_sorted_vec(), [-20, -10, 1, 2, 3, 3, 5, 43]);
    assert!(b.is_empty());
}
