// Extracted from library/core/src/task/poll.rs:224
#![allow(unused)]
fn main() {
    use core::task::Poll;
    assert_eq!(Poll::from(true), Poll::Ready(true));
}
