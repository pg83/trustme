// Extracted from library/alloc/src/collections/binary_heap/mod.rs:1124
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    let mut heap = BinaryHeap::new();
    heap.reserve_exact(100);
    assert!(heap.capacity() >= 100);
    heap.push(4);
}
