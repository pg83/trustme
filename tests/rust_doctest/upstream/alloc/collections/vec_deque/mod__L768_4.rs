// Extracted from library/alloc/src/collections/vec_deque/mod.rs:768
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let buf: VecDeque<i32> = VecDeque::with_capacity(10);
    assert!(buf.capacity() >= 10);
}
