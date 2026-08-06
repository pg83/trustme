// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1865
#![allow(unused)]
#![feature(vec_deque_pop_if)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut deque: VecDeque<i32> = vec![0, 1, 2, 3, 4].into();
    let pred = |x: &mut i32| *x % 2 == 0;
    
    assert_eq!(deque.pop_back_if(pred), Some(4));
    assert_eq!(deque, [0, 1, 2, 3]);
    assert_eq!(deque.pop_back_if(pred), None);
}
