// Extracted from library/alloc/src/collections/binary_heap/mod.rs:1360
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    let mut heap = BinaryHeap::new();

    assert!(heap.is_empty());

    heap.push(3);
    heap.push(5);
    heap.push(1);

    assert!(!heap.is_empty());
}
