// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1328
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut deque = VecDeque::new();

    deque.push_back(0);
    deque.push_back(1);
    deque.push_back(2);

    let expected = [0, 1, 2];
    let (front, back) = deque.as_slices();
    assert_eq!(&expected[..front.len()], front);
    assert_eq!(&expected[front.len()..], back);

    deque.push_front(10);
    deque.push_front(9);

    let expected = [9, 10, 0, 1, 2];
    let (front, back) = deque.as_slices();
    assert_eq!(&expected[..front.len()], front);
    assert_eq!(&expected[front.len()..], back);
}
