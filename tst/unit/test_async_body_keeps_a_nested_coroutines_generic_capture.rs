#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]
use std::future::Future;
use std::ops::{Coroutine, CoroutineState};
use std::pin::Pin;
use std::task::{Context, Poll, Waker};

fn make<const N: usize>(seed: u16) -> impl Future<Output = u16> {
    async move {
        let mut inner = #[coroutine] move || { yield seed; seed + N as u16 };
        let first = match Pin::new(&mut inner).resume(()) {
            CoroutineState::Yielded(value) => value,
            CoroutineState::Complete(_) => 0,
        };
        let second = match Pin::new(&mut inner).resume(()) {
            CoroutineState::Complete(value) => value,
            CoroutineState::Yielded(_) => 0,
        };
        first + second
    }
}

fn main() {
    let mut future = std::pin::pin!(make::<4>(19));
    let mut cx = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut cx), Poll::Ready(42));
}
