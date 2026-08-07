// Extracted from library/alloc/src/collections/binary_heap/mod.rs:989
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;

    let mut heap = BinaryHeap::from([-10, -5, 1, 2, 4, 13]);

    heap.retain(|x| x % 2 == 0); // only keep even numbers

    assert_eq!(heap.into_sorted_vec(), [-10, 2, 4])
}
