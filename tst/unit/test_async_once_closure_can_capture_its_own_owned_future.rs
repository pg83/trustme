use std::future::Future;
use std::task::{Context, Poll, Waker};

fn factory() -> impl AsyncFnOnce() -> u32 {
    let marker = Box::new(13);
    async move || {
        let pending = factory()();
        let carry = move || pending;
        drop(carry);
        let result = *marker;
        drop(marker);
        result
    }
}

fn main() {
    let mut pinned = std::pin::pin!(factory()());
    let mut context = Context::from_waker(Waker::noop());
    assert!(matches!(pinned.as_mut().poll(&mut context), Poll::Ready(13)));
}
