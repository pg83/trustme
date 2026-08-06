// Extracted from library/alloc/src/collections/vec_deque/mod.rs:956
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut buf = VecDeque::with_capacity(15);
    buf.extend(0..4);
    assert_eq!(buf.capacity(), 15);
    buf.shrink_to_fit();
    assert!(buf.capacity() >= 4);
}
