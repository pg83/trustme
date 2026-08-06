// Extracted from library/alloc/src/collections/binary_heap/mod.rs:1413
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    let mut heap = BinaryHeap::from([1, 3]);
    
    assert!(!heap.is_empty());
    
    heap.clear();
    
    assert!(heap.is_empty());
}
