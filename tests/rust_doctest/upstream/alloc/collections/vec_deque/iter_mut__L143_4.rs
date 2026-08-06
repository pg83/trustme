// Extracted from library/alloc/src/collections/vec_deque/iter_mut.rs:143
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::vec_deque;
    let iter: vec_deque::IterMut<'_, u8> = Default::default();
    assert_eq!(iter.len(), 0);
}
