// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2763
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let deque: VecDeque<_> = [0, 1, 1, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55].into();

    assert_eq!(deque.binary_search(&13),  Ok(9));
    assert_eq!(deque.binary_search(&4),   Err(7));
    assert_eq!(deque.binary_search(&100), Err(13));
    let r = deque.binary_search(&1);
    assert!(matches!(r, Ok(1..=4)));
}
