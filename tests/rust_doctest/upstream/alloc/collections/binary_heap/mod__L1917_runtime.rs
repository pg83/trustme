// Extracted from library/alloc/src/collections/binary_heap/mod.rs:1917
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    let heap = BinaryHeap::from([1, 2, 3, 4]);
    
    // Print 1, 2, 3, 4 in arbitrary order
    for x in heap.into_iter() {
        // x has type i32, not &i32
        println!("{x}");
    }
}
