// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2092
#![allow(unused)]
#![feature(push_mut)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut vec_deque = VecDeque::from([1, 2, 3]);

    let x = vec_deque.insert_mut(1, 5);
    *x += 7;
    assert_eq!(vec_deque, &[1, 12, 2, 3]);
}
