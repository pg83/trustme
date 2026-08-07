// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1989
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut buf = VecDeque::new();
    assert_eq!(buf.swap_remove_front(0), None);
    buf.push_back(1);
    buf.push_back(2);
    buf.push_back(3);
    assert_eq!(buf, [1, 2, 3]);

    assert_eq!(buf.swap_remove_front(2), Some(3));
    assert_eq!(buf, [2, 1]);
}
