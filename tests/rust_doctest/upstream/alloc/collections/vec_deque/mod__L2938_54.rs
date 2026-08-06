// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2938
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut deque: VecDeque<_> = [0, 1, 1, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55].into();
    let num = 42;
    let idx = deque.partition_point(|&x| x < num);
    deque.insert(idx, num);
    assert_eq!(deque, &[0, 1, 1, 1, 1, 2, 3, 5, 8, 13, 21, 34, 42, 55]);
}
