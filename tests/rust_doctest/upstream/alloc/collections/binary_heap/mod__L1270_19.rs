// Extracted from library/alloc/src/collections/binary_heap/mod.rs:1270
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    let mut heap: BinaryHeap<i32> = BinaryHeap::with_capacity(100);
    
    assert!(heap.capacity() >= 100);
    heap.shrink_to(10);
    assert!(heap.capacity() >= 10);
}
