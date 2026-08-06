// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2059
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut vec_deque = VecDeque::new();
    vec_deque.push_back('a');
    vec_deque.push_back('b');
    vec_deque.push_back('c');
    assert_eq!(vec_deque, &['a', 'b', 'c']);
    
    vec_deque.insert(1, 'd');
    assert_eq!(vec_deque, &['a', 'd', 'b', 'c']);
    
    vec_deque.insert(4, 'e');
    assert_eq!(vec_deque, &['a', 'd', 'b', 'c', 'e']);
}
