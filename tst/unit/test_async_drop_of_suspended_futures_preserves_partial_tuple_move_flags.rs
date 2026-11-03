//@ compile-variant: -O
#![feature(async_drop)]
use std::cell::Cell;
use std::future::{async_drop_in_place, pending, poll_fn, AsyncDrop, Future};
use std::mem::ManuallyDrop;
use std::pin::{pin, Pin};
use std::task::{Context, Poll, Waker};
struct Deferred<'a>(&'a Cell<u16>, u16);
impl Drop for Deferred<'_> { fn drop(&mut self) { self.0.set(999); } }
impl AsyncDrop for Deferred<'_> {
    async fn drop(self: Pin<&mut Self>) {
        let mut waiting = true;
        poll_fn(|_| {
            if waiting { waiting = false; Poll::Pending } else { Poll::Ready(()) }
        }).await;
        self.0.set(self.0.get() * 10 + self.1);
    }
}
async fn paused(events: &Cell<u16>, forget_first: bool) {
    let values = (Deferred(events, 1), Deferred(events, 2));
    if forget_first { std::mem::forget(values.0); }
    pending::<()>().await;
}
fn main() {
    let mut context = Context::from_waker(Waker::noop());
    for forget_first in [false, true] {
        let events = Cell::new(0);
        let mut future = ManuallyDrop::new(paused(&events, std::hint::black_box(forget_first)));
        {
            let mut pinned = unsafe { Pin::new_unchecked(&mut *future) };
            assert!(pinned.as_mut().poll(&mut context).is_pending());
        }
        let mut dropping = pin!(unsafe { async_drop_in_place(&mut *future) });
        assert!(dropping.as_mut().poll(&mut context).is_pending());
        if !forget_first { assert!(dropping.as_mut().poll(&mut context).is_pending()); }
        assert!(dropping.as_mut().poll(&mut context).is_ready());
        assert_eq!(events.get(), if forget_first { 2 } else { 12 });
    }
}
