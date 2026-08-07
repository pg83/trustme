// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1783
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut d = VecDeque::new();
    d.push_back(1);
    d.push_back(2);

    assert_eq!(d.pop_front(), Some(1));
    assert_eq!(d.pop_front(), Some(2));
    assert_eq!(d.pop_front(), None);
}
