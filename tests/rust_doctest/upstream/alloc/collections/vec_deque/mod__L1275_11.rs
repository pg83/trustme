// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1275
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut buf = VecDeque::new();
    buf.push_back(5);
    buf.push_back(3);
    buf.push_back(4);
    let b: &[_] = &[&5, &3, &4];
    let c: Vec<&i32> = buf.iter().collect();
    assert_eq!(&c[..], b);
}
