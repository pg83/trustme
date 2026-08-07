// Extracted from library/alloc/src/collections/vec_deque/iter_mut.rs:105
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

    iter.as_mut_slices().0[0] = 42;
    iter.as_mut_slices().1[0] = 24;
    assert_eq!(deque.as_slices(), (&[8, 42, 10][..], &[24, 1, 2][..]));
}
