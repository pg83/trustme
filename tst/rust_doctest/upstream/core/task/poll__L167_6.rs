// Extracted from library/core/src/task/poll.rs:167
#![allow(unused)]
fn main() {
    use core::task::Poll;
    let res: Poll<Option<Result<u8, _>>> = Poll::Ready(Some("12".parse()));
    let squared = res.map_ok(|n| n * n);
    assert_eq!(squared, Poll::Ready(Some(Ok(144))));
}
