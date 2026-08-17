//@ edition: 2024
// `for await x in it` polls an async iterator: the loop suspends while the
// iterator is not ready, and ends when it hands back `None`.
#![feature(async_iterator, async_iter_from_iter, async_for_loop, gen_blocks)]

use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};

async gen fn doubled() -> i32 {
    for await i in core::async_iter::from_iter(1..4) {
        yield i * 2;
    }
}

async fn sum() -> i32 {
    let mut total = 0;
    let mut count = 0;
    for await i in doubled() {
        total += i;
        count += 1;
    }
    assert_eq!(count, 3);
    // `for await` over an empty iterator runs its body no times.
    for await i in core::async_iter::from_iter(0..0) {
        total += i;
    }
    total
}

fn main() {
    let ctx = &mut Context::from_waker(Waker::noop());
    let mut future = pin!(sum());
    loop {
        match future.as_mut().poll(ctx) {
            Poll::Ready(total) => {
                assert_eq!(total, 12);
                return;
            }
            Poll::Pending => {}
        }
    }
}
