// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2320
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut buf = VecDeque::new();
    buf.extend(1..6);

    let keep = [false, true, true, false, true];
    let mut iter = keep.iter();
    buf.retain(|_| *iter.next().unwrap());
    assert_eq!(buf, [2, 3, 5]);
}
