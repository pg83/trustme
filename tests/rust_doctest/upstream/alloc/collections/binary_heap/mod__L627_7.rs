// Extracted from library/alloc/src/collections/binary_heap/mod.rs:627
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    let mut heap = BinaryHeap::from([1, 3]);
    
    assert_eq!(heap.pop(), Some(3));
    assert_eq!(heap.pop(), Some(1));
    assert_eq!(heap.pop(), None);
}
