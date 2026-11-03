//@ aux-build: async_tuple_library.rs
//@ input: aux/tuple_drop_log.rs
#![feature(async_drop)]
extern crate async_tuple_library;
use std::cell::Cell;
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};
fn main() {
    let mut context = Context::from_waker(Waker::noop());
    for forget_first in [false, true] {
        let order = Cell::new(0);
        let mut future = pin!(async_tuple_library::finish(&order, 751u32, std::hint::black_box(forget_first)));
        assert_eq!(future.as_mut().poll(&mut context), Poll::Pending);
        assert_eq!(order.get(), 0);
        assert_eq!(future.as_mut().poll(&mut context), Poll::Pending);
        if !forget_first {
            assert_eq!(order.get(), 0);
            assert_eq!(future.as_mut().poll(&mut context), Poll::Pending);
            assert_eq!(order.get(), 12);
        } else {
            assert_eq!(order.get(), 2);
        }
        assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(751));
        assert_eq!(order.get(), if forget_first { 23 } else { 123 });
    }
}
