// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1641
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut deque = VecDeque::new();
    deque.push_back(1);
    deque.clear();
    assert!(deque.is_empty());
}
