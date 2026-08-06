// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1141
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut buf = VecDeque::new();
    buf.push_back(5);
    buf.push_back(10);
    buf.push_back(15);
    assert_eq!(buf, [5, 10, 15]);
    buf.truncate(1);
    assert_eq!(buf, [5]);
}
