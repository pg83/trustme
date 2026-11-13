use std::future::Future;
use std::task::{Context, Poll, Waker};

fn factory() -> impl AsyncFn() -> u32 {
    async || {
        let task = factory();
        let pending = task();
        let carry = move || pending;
        drop(carry);
        9
    }
}

fn main() {
    let task = factory();
    let future = task();
    let mut pinned = std::pin::pin!(future);
    let mut context = Context::from_waker(Waker::noop());
    assert!(matches!(pinned.as_mut().poll(&mut context), Poll::Ready(9)));
}
