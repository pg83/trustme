// Extracted from library/core/src/task/poll.rs:64
#![allow(unused)]
fn main() {
    use core::task::Poll;
    let x: Poll<u32> = Poll::Ready(2);
    assert_eq!(x.is_ready(), true);

    let x: Poll<u32> = Poll::Pending;
    assert_eq!(x.is_ready(), false);
}
