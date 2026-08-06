// Extracted from library/alloc/src/collections/vec_deque/iter.rs:70
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::vec_deque;
    let iter: vec_deque::Iter<'_, u8> = Default::default();
    assert_eq!(iter.len(), 0);
}
