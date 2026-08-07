// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2487
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut buf = VecDeque::new();

    buf.push_back(2);
    buf.push_back(1);
    buf.push_front(3);

    buf.make_contiguous();
    if let (slice, &[]) = buf.as_slices() {
        // we can now be sure that `slice` contains all elements of the deque,
        // while still having immutable access to `buf`.
        assert_eq!(buf.len(), slice.len());
        assert_eq!(slice, &[3, 2, 1] as &[_]);
    }
}
