//@ compile-variant: -O
#![feature(box_patterns)]
use std::future::Future;
use std::task::{Context, Poll, Waker};
async fn selected((box value,): (Box<u32>,)) -> u32 { value }
fn main() {
    let mut future = std::pin::pin!(selected((Box::new(107),)));
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(107));
}
