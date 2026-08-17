//@ edition: 2024
// `async gen` is a coroutine that both awaits and yields: it is an
// AsyncIterator, and it is fused once its body has run out.
#![feature(gen_blocks, async_iterator)]

use std::async_iter::AsyncIterator;
use std::pin::pin;
use std::task::{Context, Poll, Waker};

async fn one() -> i32 {
    1
}

async gen fn counted() -> i32 {
    yield one().await;
    yield 2;
    yield 3;
}

fn deduced() -> impl AsyncIterator<Item = u8> {
    async gen {
        yield Default::default();
        yield 4;
    }
}

/// Drain an async iterator with a waker that never wakes: the poll loop just
/// spins on `Pending`.
fn drain<I: AsyncIterator>(iter: I) -> Vec<I::Item> {
    let ctx = &mut Context::from_waker(Waker::noop());
    let mut iter = pin!(iter);
    let mut out = Vec::new();
    loop {
        match iter.as_mut().poll_next(ctx) {
            Poll::Ready(Some(item)) => out.push(item),
            Poll::Ready(None) => return out,
            Poll::Pending => {}
        }
    }
}

fn main() {
    assert_eq!(drain(counted()), vec![1, 2, 3]);
    assert_eq!(drain(deduced()), vec![0, 4]);

    // Polling past the end keeps handing back `None`.
    let ctx = &mut Context::from_waker(Waker::noop());
    let mut empty = pin!(counted());
    for _ in 0..8 {
        let _ = empty.as_mut().poll_next(ctx);
    }
    assert_eq!(empty.as_mut().poll_next(ctx), Poll::Ready(None));
}
