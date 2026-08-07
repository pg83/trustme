// Extracted from library/alloc/src/collections/vec_deque/mod.rs:2347
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::VecDeque;

    let mut buf = VecDeque::new();
    buf.extend(1..5);
    buf.retain_mut(|x| if *x % 2 == 0 {
        *x += 1;
        true
    } else {
        false
    });
    assert_eq!(buf, [3, 5]);
}
