// sse-stream 0.3: `sse_sequence[receive_count]` in an async test whose
// `receive_count` lives across an `.await`. The index operand is a
// temporary of its own, so the saved variable is read into it.
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, RawWaker, RawWakerVTable, Waker};

fn noop_raw() -> RawWaker {
    fn clone(_: *const ()) -> RawWaker {
        noop_raw()
    }
    fn noop(_: *const ()) {}
    static VTABLE: RawWakerVTable = RawWakerVTable::new(clone, noop, noop, noop);
    RawWaker::new(std::ptr::null(), &VTABLE)
}

fn block_on<F: Future>(fut: F) -> F::Output {
    let waker = unsafe { Waker::from_raw(noop_raw()) };
    let mut cx = Context::from_waker(&waker);
    let mut fut = pin!(fut);
    loop {
        if let Poll::Ready(v) = fut.as_mut().poll(&mut cx) {
            return v;
        }
    }
}

async fn step() -> usize {
    1
}

async fn walk() -> usize {
    let values = [10usize, 20, 30];
    let mut receive_count = 0;
    let mut sum = 0;
    while receive_count < values.len() {
        let next = step().await;
        sum += values[receive_count];
        receive_count += next;
    }
    sum
}

fn main() {
    assert_eq!(block_on(walk()), 60);
}
