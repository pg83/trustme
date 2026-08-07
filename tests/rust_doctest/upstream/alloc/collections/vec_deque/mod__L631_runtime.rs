// Extracted from library/alloc/src/collections/vec_deque/mod.rs:631
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let deque: VecDeque<u32> = VecDeque::with_capacity(10);
}
