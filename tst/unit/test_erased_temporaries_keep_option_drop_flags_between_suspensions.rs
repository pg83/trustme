//@ compile-variant: -O
use std::cell::Cell;
use std::future::{poll_fn, Future};
use std::task::{Context, Poll, Waker};
struct Guard<'a>(&'a Cell<u16>);
impl Drop for Guard<'_> { fn drop(&mut self) { self.0.set(self.0.get() + 3); } }
async fn pause() {
    let mut waiting = true;
    poll_fn(|_| { if waiting { waiting = false; Poll::Pending } else { Poll::Ready(()) } }).await;
}
async fn job(events: &Cell<u16>, take: bool) -> bool {
    let mut value = Some(Guard(events));
    let selected = if take { value.take() } else { None };
    pause().await;
    drop(selected);
    let remains = value.is_some();
    pause().await;
    remains
}
fn main() {
    let mut context = Context::from_waker(Waker::noop());
    for take in [false, true] {
        for complete in [false, true] {
            let events = Cell::new(0);
            let mut future = Box::pin(job(&events, take));
            assert!(future.as_mut().poll(&mut context).is_pending());
            assert_eq!(events.get(), 0);
            assert!(future.as_mut().poll(&mut context).is_pending());
            assert_eq!(events.get(), if take { 3 } else { 0 });
            if complete { assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(!take)); }
            drop(future);
            assert_eq!(events.get(), 3);
        }
    }
}
