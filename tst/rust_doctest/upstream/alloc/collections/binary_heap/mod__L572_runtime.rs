// Extracted from library/alloc/src/collections/binary_heap/mod.rs:572
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {

    use std::alloc::System;
    use std::collections::BinaryHeap;
    let mut heap = BinaryHeap::with_capacity_in(10, System);
    heap.push(4);
}
