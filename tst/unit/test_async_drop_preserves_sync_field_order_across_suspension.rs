#![feature(async_drop)]
use std::cell::Cell;
use std::future::{async_drop_in_place, poll_fn, AsyncDrop, Future};
use std::mem::MaybeUninit;
use std::pin::{pin, Pin};
use std::task::{Context, Poll, Waker};
struct Sync<'a>(&'a Cell<u64>, u64);
impl Drop for Sync<'_> { fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); } }
struct Async<'a>(&'a Cell<u64>, u64);
impl Drop for Async<'_> { fn drop(&mut self) { self.0.set(9999); } }
impl AsyncDrop for Async<'_> {
    async fn drop(self: Pin<&mut Self>) {
        let mut paused = false;
        poll_fn(|_| { if paused { Poll::Ready(()) } else { paused = true; Poll::Pending } }).await;
        self.0.set(self.0.get() * 10 + self.1);
    }
}
struct Packet<'a> { first: Sync<'a>, middle: Async<'a>, last: Sync<'a> }
fn main() {
    let log = Cell::new(0);
    let mut storage = MaybeUninit::new(Packet { first: Sync(&log,1), middle: Async(&log,2), last: Sync(&log,3) });
    let mut dropping = pin!(unsafe { async_drop_in_place(storage.as_mut_ptr()) });
    let mut context = Context::from_waker(Waker::noop());
    assert!(dropping.as_mut().poll(&mut context).is_pending());
    assert_eq!(log.get(),1);
    assert!(dropping.as_mut().poll(&mut context).is_ready());
    assert_eq!(log.get(),123);
}
