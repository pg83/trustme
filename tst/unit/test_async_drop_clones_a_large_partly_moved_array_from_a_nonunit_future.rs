#![feature(async_drop)]
#![allow(incomplete_features)]

use std::cell::Cell;
use std::future::{async_drop_in_place, pending, poll_fn, AsyncDrop, Future};
use std::mem::ManuallyDrop;
use std::pin::{pin, Pin};
use std::task::{Context, Poll, Waker};

struct Item<'a>(&'a Cell<u32>, u32);

impl Drop for Item<'_> {
    fn drop(&mut self) {
        self.0.set(100_000);
    }
}

impl AsyncDrop for Item<'_> {
    async fn drop(self: Pin<&mut Self>) {
        let mut first = true;
        poll_fn(|_| {
            if first {
                first = false;
                Poll::Pending
            } else {
                Poll::Ready(())
            }
        }).await;
        self.0.set(self.0.get() + self.1);
    }
}

async fn suspended(log: &Cell<u32>) -> [u16; 2] {
    let items: [Item<'_>; 33] = std::array::from_fn(|i| Item(log, i as u32 + 1));
    let [first, ..] = items;
    std::mem::forget(first);
    pending::<()>().await;
    [41, 59]
}

fn main() {
    let log = Cell::new(0);
    let mut context = Context::from_waker(Waker::noop());
    let mut value = ManuallyDrop::new(suspended(&log));
    assert!(unsafe { Pin::new_unchecked(&mut *value) }.poll(&mut context).is_pending());
    let mut cleanup = pin!(unsafe { async_drop_in_place(&mut *value) });
    let mut pauses = 0;
    while cleanup.as_mut().poll(&mut context).is_pending() {
        pauses += 1;
    }
    assert_eq!(pauses, 32);
    assert_eq!(log.get(), 560);
}
