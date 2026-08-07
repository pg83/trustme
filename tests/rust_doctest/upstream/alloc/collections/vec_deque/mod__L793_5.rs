// Extracted from library/alloc/src/collections/vec_deque/mod.rs:793
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut buf: VecDeque<i32> = [1].into();
    buf.reserve_exact(10);
    assert!(buf.capacity() >= 11);
}
