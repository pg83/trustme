// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2418
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut buf = VecDeque::new();
    buf.push_back(5);
    buf.push_back(10);
    buf.push_back(15);
    assert_eq!(buf, [5, 10, 15]);
    
    buf.resize_with(5, Default::default);
    assert_eq!(buf, [5, 10, 15, 0, 0]);
    
    buf.resize_with(2, || unreachable!());
    assert_eq!(buf, [5, 10]);
    
    let mut state = 100;
    buf.resize_with(5, || { state += 1; state });
    assert_eq!(buf, [5, 10, 101, 102, 103]);
}
