// Extracted from library/alloc/src/collections/binary_heap/mod.rs:1872
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    
    let mut h1 = BinaryHeap::from([1, 4, 2, 3]);
    let mut h2: BinaryHeap<_> = [1, 4, 2, 3].into();
    while let Some((a, b)) = h1.pop().zip(h2.pop()) {
        assert_eq!(a, b);
    }
}
