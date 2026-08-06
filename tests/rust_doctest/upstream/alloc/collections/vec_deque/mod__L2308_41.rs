// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2308
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut buf = VecDeque::new();
    buf.extend(1..5);
    buf.retain(|&x| x % 2 == 0);
    assert_eq!(buf, [2, 4]);
}
