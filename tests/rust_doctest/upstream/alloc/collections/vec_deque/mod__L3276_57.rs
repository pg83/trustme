// Extracted from library/alloc/src/collections/vec_deque/mod.rs:3276
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let deq1 = VecDeque::from([1, 2, 3, 4]);
    let deq2: VecDeque<_> = [1, 2, 3, 4].into();
    assert_eq!(deq1, deq2);
}
