// Extracted from library/alloc/src/collections/binary_heap/mod.rs:657
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    let mut heap = BinaryHeap::new();
    heap.push(3);
    heap.push(5);
    heap.push(1);
    
    assert_eq!(heap.len(), 3);
    assert_eq!(heap.peek(), Some(&5));
}
