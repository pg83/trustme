// Extracted from library/alloc/src/collections/binary_heap/mod.rs:1068
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    let mut heap = BinaryHeap::new();
    assert_eq!(heap.peek(), None);
    
    heap.push(1);
    heap.push(5);
    heap.push(2);
    assert_eq!(heap.peek(), Some(&5));
}
