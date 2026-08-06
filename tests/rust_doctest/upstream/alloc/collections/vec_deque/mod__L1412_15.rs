// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1412
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut deque = VecDeque::new();
    assert_eq!(deque.len(), 0);
    deque.push_back(1);
    assert_eq!(deque.len(), 1);
}
