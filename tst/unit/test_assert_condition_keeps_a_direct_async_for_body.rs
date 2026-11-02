//@ edition: 2024
//@ compile-variant: -O
#![feature(async_iterator, async_iter_from_iter, async_for_loop)]
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};
fn main() {
    assert!({
        let future = async {
            let mut values = Vec::new();
            for await value in std::async_iter::from_iter([17u16, 23]) {
                values.push(value + 1);
            }
            values
        };
        let mut future = pin!(future);
        let mut context = Context::from_waker(Waker::noop());
        match future.as_mut().poll(&mut context) {
            Poll::Ready(values) => values == [18, 24],
            Poll::Pending => false,
        }
    });
}
