// Extracted from library/alloc/src/collections/binary_heap/mod.rs:1389
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    let mut heap = BinaryHeap::from([1, 3]);
    
    assert!(!heap.is_empty());
    
    for x in heap.drain() {
        println!("{x}");
    }
    
    assert!(heap.is_empty());
}
