// Extracted from library/alloc/src/collections/binary_heap/mod.rs:1152
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    let mut heap = BinaryHeap::new();
    heap.reserve(100);
    assert!(heap.capacity() >= 100);
    heap.push(4);
}
