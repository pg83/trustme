// Extracted from library/core/src/task/poll.rs:110
#![allow(unused)]
fn main() {
    use core::task::Poll;
    let res: Poll<Result<u8, _>> = Poll::Ready("12".parse());
    let squared = res.map_ok(|n| n * n);
    assert_eq!(squared, Poll::Ready(Ok(144)));
}
