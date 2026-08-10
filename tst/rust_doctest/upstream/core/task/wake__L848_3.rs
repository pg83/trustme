// Extracted from library/core/src/task/wake.rs:848
#![allow(unused)]
#![feature(local_waker)]
fn main() {
    use std::future::Future;
    use std::task::{ContextBuilder, LocalWaker, Waker, Poll};

    let mut cx = ContextBuilder::from_waker(Waker::noop())
        .local_waker(LocalWaker::noop())
        .build();

    let mut future = Box::pin(async { 10 });
    assert_eq!(future.as_mut().poll(&mut cx), Poll::Ready(10));
}
