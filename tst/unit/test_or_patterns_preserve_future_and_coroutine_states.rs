#![feature(coroutines, coroutine_trait, stmt_expr_attributes)]
use std::ops::{Coroutine, CoroutineState};
use std::future::Future;
use std::task::{Context, Poll, Waker};
fn main() {
    let future = async { 37u32 };
    let coroutine = #[coroutine] || { yield 17u32; 23u32 };
    for choice in [0u8, 1, 2] {
        let a = match (choice, &future) { (0 | 1, &_) => 41, _ => 53 };
        let b = match (choice, &coroutine) { (0 | 1, &_) => 41, _ => 53 };
        assert_eq!((a, b), if choice < 2 { (41, 41) } else { (53, 53) });
    }
    let mut future = std::pin::pin!(future);
    assert_eq!(future.as_mut().poll(&mut Context::from_waker(Waker::noop())), Poll::Ready(37));
    let mut coroutine = std::pin::pin!(coroutine);
    assert_eq!(coroutine.as_mut().resume(()), CoroutineState::Yielded(17));
    assert_eq!(coroutine.as_mut().resume(()), CoroutineState::Complete(23));
}
