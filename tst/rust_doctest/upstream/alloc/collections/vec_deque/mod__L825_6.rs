// Extracted from library/alloc/src/collections/vec_deque/mod.rs:825
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut buf: VecDeque<i32> = [1].into();
    buf.reserve(10);
    assert!(buf.capacity() >= 11);
}
