// Extracted from library/alloc/src/collections/binary_heap/mod.rs:700
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;

    let mut heap = BinaryHeap::from([1, 2, 4, 5, 7]);
    heap.push(6);
    heap.push(3);

    let vec = heap.into_sorted_vec();
    assert_eq!(vec, [1, 2, 3, 4, 5, 6, 7]);
}
