// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1583
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut deque: VecDeque<_> = [1, 2, 3].into();
    let drained = deque.drain(2..).collect::<VecDeque<_>>();
    assert_eq!(drained, [3]);
    assert_eq!(deque, [1, 2]);
    
    // A full range clears all contents, like `clear()` does
    deque.drain(..);
    assert!(deque.is_empty());
}
