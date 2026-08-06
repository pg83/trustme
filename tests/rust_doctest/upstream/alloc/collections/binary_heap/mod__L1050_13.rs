// Extracted from library/alloc/src/collections/binary_heap/mod.rs:1050
#![allow(unused)]
#![feature(binary_heap_into_iter_sorted)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    let heap = BinaryHeap::from([1, 2, 3, 4, 5]);
    
    assert_eq!(heap.into_iter_sorted().take(2).collect::<Vec<_>>(), [5, 4]);
}
