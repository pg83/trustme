//@ edition: 2021
// A value bounded only by an async callable trait is called through it: the call
// evaluates to a future, and the future an async closure returns takes the
// captures with it rather than borrowing the frame of the call that made it.
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};

fn block_on<T>(future: impl Future<Output = T>) -> T {
    let mut future = pin!(future);
    let ctx = &mut Context::from_waker(Waker::noop());
    loop {
        match future.as_mut().poll(ctx) {
            Poll::Ready(value) => return value,
            Poll::Pending => {}
        }
    }
}

async fn twice(f: impl AsyncFn(u64) -> u64) -> u64 {
    f(1).await + f(2).await
}

async fn once<T>(f: impl AsyncFnOnce() -> T) -> T {
    f().await
}

fn main() {
    assert_eq!(block_on(twice(async |i| i * 10)), 30);
    assert_eq!(block_on(once(async || "done")), "done");

    // A capture is read from inside the future, after the call that made it has
    // returned.
    let base = 100u64;
    assert_eq!(block_on(twice(async |i| i + base)), 203);

    assert_eq!(block_on(directly()), 203);
}

/// The same, called directly rather than through a bound.
async fn directly() -> u64 {
    let base = 100u64;
    let c = async |i: u64| i + base;
    c(1).await + c(2).await
}
