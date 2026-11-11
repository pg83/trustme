#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]

use std::future::Future;
use std::ops::{Coroutine, CoroutineState};
use std::task::{Context, Poll, Waker};

fn main() {
    let mut outer = std::pin::pin!(#[coroutine] || {
        yield 7u16;
        11u16
    });
    assert!(matches!(outer.as_mut().resume(()), CoroutineState::Yielded(7)));
    assert!(matches!(outer.as_mut().resume(()), CoroutineState::Complete(11)));

    let mut future = std::pin::pin!(async {
        let mut inner = std::pin::pin!(#[coroutine] || {
            yield 13u16;
            17u16
        });
        assert!(matches!(inner.as_mut().resume(()), CoroutineState::Yielded(13)));
        match inner.as_mut().resume(()) {
            CoroutineState::Complete(value) => value + 6,
            CoroutineState::Yielded(_) => panic!("unexpected second yield"),
        }
    });
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(23));
}
