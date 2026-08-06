// Extracted from library/alloc/src/collections/binary_heap/mod.rs:964
#![allow(unused)]
#![feature(binary_heap_drain_sorted)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    
    let mut heap = BinaryHeap::from([1, 2, 3, 4, 5]);
    assert_eq!(heap.len(), 5);
    
    drop(heap.drain_sorted()); // removes all elements in heap order
    assert_eq!(heap.len(), 0);
}
