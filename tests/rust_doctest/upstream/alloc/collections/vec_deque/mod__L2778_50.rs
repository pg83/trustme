// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2778
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut deque: VecDeque<_> = [0, 1, 1, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55].into();
    let num = 42;
    let idx = deque.partition_point(|&x| x <= num);
    // If `num` is unique, `s.partition_point(|&x| x < num)` (with `<`) is equivalent to
    // `s.binary_search(&num).unwrap_or_else(|x| x)`, but using `<=` may allow `insert`
    // to shift less elements.
    deque.insert(idx, num);
    assert_eq!(deque, &[0, 1, 1, 1, 1, 2, 3, 5, 8, 13, 21, 34, 42, 55]);
}
