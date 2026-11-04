//@ compile-variant: -O
use std::future::{ready, Future};
use std::ops::AsyncFn;
use std::pin::pin;
use std::task::{Context, Poll, Waker};
async fn invoke<F: AsyncFn(u16) -> u16>(f: F) -> u16 { f(7).await + f(9).await }
fn main() {
    let offset = 5u16;
    let callback = |value: u16| ready(value + offset);
    let mut future = pin!(invoke(callback));
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(26));
    assert_eq!(offset, 5);
}
