// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1430
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut deque = VecDeque::new();
    assert!(deque.is_empty());
    deque.push_front(1);
    assert!(!deque.is_empty());
}
