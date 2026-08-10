// Extracted from library/alloc/src/collections/binary_heap/mod.rs:548
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {

    use std::alloc::System;
    use std::collections::BinaryHeap;
    let mut heap = BinaryHeap::new_in(System);
    heap.push(4);
}
