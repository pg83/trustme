// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2881
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let deque: VecDeque<_> = [(0, 0), (2, 1), (4, 1), (5, 1),
             (3, 1), (1, 2), (2, 3), (4, 5), (5, 8), (3, 13),
             (1, 21), (2, 34), (4, 55)].into();

    assert_eq!(deque.binary_search_by_key(&13, |&(a, b)| b),  Ok(9));
    assert_eq!(deque.binary_search_by_key(&4, |&(a, b)| b),   Err(7));
    assert_eq!(deque.binary_search_by_key(&100, |&(a, b)| b), Err(13));
    let r = deque.binary_search_by_key(&1, |&(a, b)| b);
    assert!(matches!(r, Ok(1..=4)));
}
