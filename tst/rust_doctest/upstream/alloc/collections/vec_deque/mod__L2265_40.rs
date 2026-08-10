// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2265
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut buf: VecDeque<_> = [1, 2].into();
    let mut buf2: VecDeque<_> = [3, 4].into();
    buf.append(&mut buf2);
    assert_eq!(buf, [1, 2, 3, 4]);
    assert_eq!(buf2, []);
}
