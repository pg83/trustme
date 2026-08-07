// Extracted from library/alloc/src/collections/binary_heap/mod.rs:1312
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    let heap = BinaryHeap::from([1, 2, 3, 4, 5, 6, 7]);
    let vec = heap.into_vec();

    // Will print in some order
    for x in vec {
        println!("{x}");
    }
}
