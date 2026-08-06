// Extracted from library/alloc/src/collections/binary_heap/mod.rs:597
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    let mut heap = BinaryHeap::new();
    assert!(heap.peek_mut().is_none());
    
    heap.push(1);
    heap.push(5);
    heap.push(2);
    if let Some(mut val) = heap.peek_mut() {
        *val = 0;
    }
    assert_eq!(heap.peek(), Some(&2));
}
