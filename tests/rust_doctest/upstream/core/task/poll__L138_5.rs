// Extracted from library/core/src/task/poll.rs:138
#![allow(unused)]
fn main() {
    use core::task::Poll;
    let res: Poll<Result<u8, _>> = Poll::Ready("oops".parse());
    let res = res.map_err(|_| 0_u8);
    assert_eq!(res, Poll::Ready(Err(0)));
}
