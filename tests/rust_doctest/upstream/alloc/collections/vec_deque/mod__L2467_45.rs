// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2467
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;
    
    let mut buf = VecDeque::with_capacity(15);
    
    buf.push_back(2);
    buf.push_back(1);
    buf.push_front(3);
    
    // sorting the deque
    buf.make_contiguous().sort();
    assert_eq!(buf.as_slices(), (&[1, 2, 3] as &[_], &[] as &[_]));
    
    // sorting it in reverse order
    buf.make_contiguous().sort_by(|a, b| b.cmp(a));
    assert_eq!(buf.as_slices(), (&[3, 2, 1] as &[_], &[] as &[_]));
}
