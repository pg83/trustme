// Extracted from library/alloc/src/collections/vec_deque/mod.rs:707
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut buf = VecDeque::new();
    buf.push_back(3);
    buf.push_back(4);
    buf.push_back(5);
    buf.push_back(6);
    assert_eq!(buf[1], 4);
    if let Some(elem) = buf.get_mut(1) {
        *elem = 7;
    }
    assert_eq!(buf[1], 7);
}
