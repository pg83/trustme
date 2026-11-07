#![feature(async_drop)]
#![allow(incomplete_features)]

use std::cell::Cell;
use std::future::{async_drop_in_place, pending, poll_fn, AsyncDrop, Future};
use std::mem::ManuallyDrop;
use std::pin::{pin, Pin};
use std::task::{Context, Poll, Waker};

struct Payload<'a> {
    events: &'a Cell<u16>,
    weight: u16,
}

impl Drop for Payload<'_> {
    fn drop(&mut self) { self.events.set(999); }
}

impl AsyncDrop for Payload<'_> {
    async fn drop(self: Pin<&mut Self>) {
        self.events.set(self.events.get() + self.weight);
        let mut ready = false;
        poll_fn(|_| {
            if ready { Poll::Ready(()) } else { ready = true; Poll::Pending }
        }).await;
        self.events.set(self.events.get() + 1);
    }
}

async fn suspended(events: &Cell<u16>) {
    let payload = Payload { events, weight: 13 };
    pending::<()>().await;
    std::hint::black_box(&payload);
}

fn main() {
    let events = Cell::new(0);
    let mut body = ManuallyDrop::new(suspended(&events));
    let mut context = Context::from_waker(Waker::noop());
    assert!(unsafe { Pin::new_unchecked(&mut *body) }.poll(&mut context).is_pending());
    let mut dropping = pin!(unsafe { async_drop_in_place(&mut *body) });
    assert!(dropping.as_mut().poll(&mut context).is_pending());
    assert_eq!(events.get(), 13);
    assert!(dropping.as_mut().poll(&mut context).is_ready());
    assert_eq!(events.get(), 14);
}
