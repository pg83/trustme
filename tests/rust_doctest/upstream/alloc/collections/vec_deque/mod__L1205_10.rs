// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1205
#![allow(unused)]
#![feature(vec_deque_truncate_front)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut buf = VecDeque::new();
    buf.push_front(5);
    buf.push_front(10);
    buf.push_front(15);
    assert_eq!(buf, [15, 10, 5]);
    assert_eq!(buf.as_slices(), (&[15, 10, 5][..], &[][..]));
    buf.truncate_front(1);
    assert_eq!(buf.as_slices(), (&[5][..], &[][..]));
}
