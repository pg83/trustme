// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2969
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut buf = VecDeque::new();
    buf.push_back(5);
    buf.push_back(10);
    buf.push_back(15);
    assert_eq!(buf, [5, 10, 15]);

    buf.resize(2, 0);
    assert_eq!(buf, [5, 10]);

    buf.resize(5, 20);
    assert_eq!(buf, [5, 10, 20, 20, 20]);
}
