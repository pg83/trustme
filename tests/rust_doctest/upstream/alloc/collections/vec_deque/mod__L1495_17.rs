// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1495
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let deque: VecDeque<_> = [1, 2, 3].into();
    let range = deque.range(2..).copied().collect::<VecDeque<_>>();
    assert_eq!(range, [3]);

    // A full range covers all contents
    let all = deque.range(..);
    assert_eq!(all.len(), 3);
}
