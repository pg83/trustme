// Extracted from library/alloc/src/collections/binary_heap/mod.rs:396
#![allow(unused)]
#![feature(binary_heap_peek_mut_refresh)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;

    let mut heap: BinaryHeap<u32> = [1, 2, 3].into();
    let mut peek = heap.peek_mut().unwrap();

    assert_eq!(*peek, 3);
    *peek = 42;

    // When we refresh, the peek is updated to the new maximum.
    assert!(!peek.refresh(), "42 is even larger than 3");
    assert_eq!(*peek, 42);
}
