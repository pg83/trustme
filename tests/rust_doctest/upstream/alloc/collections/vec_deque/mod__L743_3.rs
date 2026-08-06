// Extracted from library/alloc/src/collections/vec_deque/mod.rs:743
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut buf = VecDeque::new();
    buf.push_back(3);
    buf.push_back(4);
    buf.push_back(5);
    assert_eq!(buf, [3, 4, 5]);
    buf.swap(0, 2);
    assert_eq!(buf, [5, 4, 3]);
}
