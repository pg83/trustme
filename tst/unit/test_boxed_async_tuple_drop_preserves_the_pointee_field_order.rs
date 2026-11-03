//@ input: aux/tuple_drop_log.rs
#![feature(async_drop)]
#[path = "aux/tuple_drop_log.rs"] mod log;
use std::cell::Cell;
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};
async fn finish(order: &Cell<usize>) {
    let value = Box::new((log::Immediate(order, 4), log::Deferred(order, 5), log::Immediate(order, 6)));
    std::hint::black_box(&value);
}
fn main() {
    let order = Cell::new(0);
    let mut future = pin!(finish(&order));
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Pending);
    assert_eq!(order.get(), 4);
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(()));
    assert_eq!(order.get(), 456);
}
