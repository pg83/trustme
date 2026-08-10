// Extracted from library/core/src/task/wake.rs:555
#![allow(unused)]
fn main() {
    use std::future::Future;
    use std::task;

    let mut cx = task::Context::from_waker(task::Waker::noop());

    let mut future = Box::pin(async { 10 });
    assert_eq!(future.as_mut().poll(&mut cx), task::Poll::Ready(10));
}
