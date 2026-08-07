// Extracted from library/core/src/future/poll_fn.rs:15
#![allow(unused)]
fn main() {
    async fn run() {
    use core::future::poll_fn;
    use std::task::{Context, Poll};

    fn read_line(_cx: &mut Context<'_>) -> Poll<String> {
        Poll::Ready("Hello, World!".into())
    }

    let read_future = poll_fn(read_line);
    assert_eq!(read_future.await, "Hello, World!".to_owned());
    }
}
