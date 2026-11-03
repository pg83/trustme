//@ input: aux/tuple_drop_log.rs
#![feature(async_drop)]
#[path = "aux/tuple_drop_log.rs"] mod log;
use std::cell::Cell;
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};
async fn finish(order: &Cell<usize>) {
    let value = (log::Deferred(order, 1), log::Immediate(order, 2), log::Deferred(order, 3));
    std::hint::black_box(&value);
}
fn main() {
    let order = Cell::new(0);
    let mut future = pin!(finish(&order));
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Pending);
    assert_eq!(order.get(), 0);
    assert_eq!(future.as_mut().poll(&mut context), Poll::Pending);
    assert_eq!(order.get(), 12);
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(()));
    assert_eq!(order.get(), 123);
}
