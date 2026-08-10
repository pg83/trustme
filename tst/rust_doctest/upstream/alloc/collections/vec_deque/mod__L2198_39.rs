// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2198
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut buf: VecDeque<_> = ['a', 'b', 'c'].into();
    let buf2 = buf.split_off(1);
    assert_eq!(buf, ['a']);
    assert_eq!(buf2, ['b', 'c']);
}
