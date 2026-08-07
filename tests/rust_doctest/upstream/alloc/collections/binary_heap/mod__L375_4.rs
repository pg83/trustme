// Extracted from library/alloc/src/collections/binary_heap/mod.rs:375
#![allow(unused)]
#![feature(binary_heap_peek_mut_refresh)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;

    let mut heap: BinaryHeap<u32> = (0..128).collect();
    let mut peek = heap.peek_mut().unwrap();

    loop {
        *peek = 99;

        if !peek.refresh() {
            break;
        }
    }

    // Post condition, this is now an upper bound.
    assert!(*peek < 100);
}
