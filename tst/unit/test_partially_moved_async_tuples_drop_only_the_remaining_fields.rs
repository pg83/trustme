//@ input: aux/tuple_drop_log.rs
#![feature(async_drop)]
#[path = "aux/tuple_drop_log.rs"] mod log;
use std::cell::Cell;
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};
async fn finish(order: &Cell<usize>, forget_first: bool) {
    let value = (log::Deferred(order, 7), log::Immediate(order, 8), log::Deferred(order, 9));
    if forget_first { core::mem::forget(value.0); }
    std::hint::black_box(&value.1);
}
fn main() {
    let order = Cell::new(0);
    let mut future = pin!(finish(&order, std::hint::black_box(true)));
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Pending);
    assert_eq!(order.get(), 8);
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(()));
    assert_eq!(order.get(), 89);
}
