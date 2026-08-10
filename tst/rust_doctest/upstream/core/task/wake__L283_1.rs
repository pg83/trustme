// Extracted from library/core/src/task/wake.rs:283
#![allow(unused)]
#![feature(local_waker)]
fn main() {
    use std::task::{ContextBuilder, LocalWaker, Waker, Poll};
    use std::future::Future;

    let local_waker = LocalWaker::noop();
    let waker = Waker::noop();

    let mut cx = ContextBuilder::from_waker(&waker)
        .local_waker(&local_waker)
        .build();

    let mut future = std::pin::pin!(async { 20 });
    let poll = future.as_mut().poll(&mut cx);
    assert_eq!(poll, Poll::Ready(20));
}
