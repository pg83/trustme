// Extracted from library/core/src/task/poll.rs:40
#![allow(unused)]
fn main() {
    use core::task::Poll;
    let poll_some_string = Poll::Ready(String::from("Hello, World!"));
    // `Poll::map` takes self *by value*, consuming `poll_some_string`
    let poll_some_len = poll_some_string.map(|s| s.len());

    assert_eq!(poll_some_len, Poll::Ready(13));
}
