// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1693
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut d = VecDeque::new();
    assert_eq!(d.front(), None);
    
    d.push_back(1);
    d.push_back(2);
    assert_eq!(d.front(), Some(&1));
}
