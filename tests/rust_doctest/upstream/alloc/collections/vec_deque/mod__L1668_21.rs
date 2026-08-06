// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1668
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut deque: VecDeque<u32> = VecDeque::new();
    
    deque.push_back(0);
    deque.push_back(1);
    
    assert_eq!(deque.contains(&1), true);
    assert_eq!(deque.contains(&10), false);
}
