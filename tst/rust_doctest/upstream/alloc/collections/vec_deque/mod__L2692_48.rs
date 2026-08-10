// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2692
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut buf: VecDeque<_> = (0..10).collect();

    buf.rotate_right(3);
    assert_eq!(buf, [7, 8, 9, 0, 1, 2, 3, 4, 5, 6]);

    for i in 1..10 {
        assert_eq!(0, buf[i * 3 % 10]);
        buf.rotate_right(3);
    }
    assert_eq!(buf, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);
}
