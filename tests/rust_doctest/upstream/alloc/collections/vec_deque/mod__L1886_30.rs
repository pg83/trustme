// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1886
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut d = VecDeque::new();
    d.push_front(1);
    d.push_front(2);
    assert_eq!(d.front(), Some(&2));
}
