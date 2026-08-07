// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1950
#![allow(unused)]
#![feature(push_mut)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut d = VecDeque::from([1, 2, 3]);
    let x = d.push_back_mut(9);
    *x += 1;
    assert_eq!(d.back(), Some(&10));
}
