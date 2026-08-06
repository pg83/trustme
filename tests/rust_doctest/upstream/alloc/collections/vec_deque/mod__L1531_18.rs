// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1531
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut deque: VecDeque<_> = [1, 2, 3].into();
    for v in deque.range_mut(2..) {
      *v *= 2;
    }
    assert_eq!(deque, [1, 2, 6]);
    
    // A full range covers all contents
    for v in deque.range_mut(..) {
      *v *= 2;
    }
    assert_eq!(deque, [2, 4, 12]);
}
