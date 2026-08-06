// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2924
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let deque: VecDeque<_> = [1, 2, 3, 3, 5, 6, 7].into();
    let i = deque.partition_point(|&x| x < 5);
    
    assert_eq!(i, 4);
    assert!(deque.iter().take(i).all(|&x| x < 5));
    assert!(deque.iter().skip(i).all(|&x| !(x < 5)));
}
