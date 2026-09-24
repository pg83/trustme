// edition:2021
// futures' async_await_macros borrows a closure from an async block. The
// capture is `&closure`, and the rewrite of closure types to their
// generated structs has to reach the capture list once the block's body
// is gone.
use std::future::Future;
use std::task::{Context, Poll, Waker};

fn block_on<F: Future>(fut: F) -> F::Output {
    let mut fut = std::pin::pin!(fut);
    let mut cx = Context::from_waker(Waker::noop());
    loop {
        if let Poll::Ready(v) = fut.as_mut().poll(&mut cx) {
            return v;
        }
    }
}

fn main() {
    let make = || 5;
    let r = block_on(async { make() + make() });
    assert_eq!(r, 10);
}
