// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2143
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut buf = VecDeque::new();
    buf.push_back('a');
    buf.push_back('b');
    buf.push_back('c');
    assert_eq!(buf, ['a', 'b', 'c']);

    assert_eq!(buf.remove(1), Some('b'));
    assert_eq!(buf, ['a', 'c']);
}
