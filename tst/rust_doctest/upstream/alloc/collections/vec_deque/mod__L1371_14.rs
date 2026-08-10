// Extracted from library/alloc/src/collections/vec_deque/mod.rs:1371
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut deque = VecDeque::new();

    deque.push_back(0);
    deque.push_back(1);

    deque.push_front(10);
    deque.push_front(9);

    // Since the split point is not guaranteed, we may need to update
    // either slice.
    let mut update_nth = |index: usize, val: u32| {
        let (front, back) = deque.as_mut_slices();
        if index > front.len() - 1 {
            back[index - front.len()] = val;
        } else {
            front[index] = val;
        }
    };

    update_nth(0, 42);
    update_nth(2, 24);

    let v: Vec<_> = deque.into();
    assert_eq!(v, [42, 10, 24, 1]);
}
