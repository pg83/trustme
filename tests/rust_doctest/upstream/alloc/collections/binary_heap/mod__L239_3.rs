// Extracted from library/alloc/src/collections/binary_heap/mod.rs:239
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BinaryHeap;
    use std::cmp::Reverse;

    let mut heap = BinaryHeap::new();

    // Wrap values in `Reverse`
    heap.push(Reverse(1));
    heap.push(Reverse(5));
    heap.push(Reverse(2));

    // If we pop these scores now, they should come back in the reverse order.
    assert_eq!(heap.pop(), Some(Reverse(1)));
    assert_eq!(heap.pop(), Some(Reverse(2)));
    assert_eq!(heap.pop(), Some(Reverse(5)));
    assert_eq!(heap.pop(), None);
}
