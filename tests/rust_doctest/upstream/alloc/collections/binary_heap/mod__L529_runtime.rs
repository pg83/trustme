// Extracted from library/alloc/src/collections/binary_heap/mod.rs:529
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    let mut heap = BinaryHeap::with_capacity(10);
    heap.push(4);
}
