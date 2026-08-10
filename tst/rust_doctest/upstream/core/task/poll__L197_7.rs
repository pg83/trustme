// Extracted from library/core/src/task/poll.rs:197
#![allow(unused)]
fn main() {
    use core::task::Poll;
    let res: Poll<Option<Result<u8, _>>> = Poll::Ready(Some("oops".parse()));
    let res = res.map_err(|_| 0_u8);
    assert_eq!(res, Poll::Ready(Some(Err(0))));
}
