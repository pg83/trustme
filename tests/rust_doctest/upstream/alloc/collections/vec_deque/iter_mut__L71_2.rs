// Extracted from library/alloc/src/collections/vec_deque/iter_mut.rs:71
#![allow(unused)]
#![feature(vec_deque_iter_as_slices)]
extern crate alloc;
fn main() {
    
    use std::collections::VecDeque;
    
    let mut deque = VecDeque::new();
    deque.push_back(0);
    deque.push_back(1);
    deque.push_back(2);
    deque.push_front(10);
    deque.push_front(9);
    deque.push_front(8);
    
    let mut iter = deque.iter_mut();
    iter.next();
    iter.next_back();
    
    assert_eq!(iter.as_slices(), (&[9, 10][..], &[0, 1][..]));
}
