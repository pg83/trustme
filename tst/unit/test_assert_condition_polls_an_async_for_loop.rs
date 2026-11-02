//@ edition: 2024
//@ compile-variant: -O
#![feature(async_iterator, async_iter_from_iter, async_for_loop)]
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};
fn main() {
    assert!({
        let mut future = pin!(async {
            let mut total = 0u16;
            for await value in std::async_iter::from_iter([17u16, 23]) {
                total += value;
            }
            total
        });
        let mut context = Context::from_waker(Waker::noop());
        matches!(future.as_mut().poll(&mut context), Poll::Ready(40))
    });
}
