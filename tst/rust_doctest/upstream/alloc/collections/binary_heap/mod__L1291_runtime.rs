// Extracted from library/alloc/src/collections/binary_heap/mod.rs:1291
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    use std::io::{self, Write};

    let heap = BinaryHeap::from([1, 2, 3, 4, 5, 6, 7]);

    io::sink().write(heap.as_slice()).unwrap();
}
