//@ compile-variant: -O
#![feature(async_drop)]
use std::cell::Cell;
use std::future::{async_drop_in_place, poll_fn, AsyncDrop, Future};
use std::mem::MaybeUninit;
use std::pin::{pin, Pin};
use std::task::{Context, Poll, Waker};
struct Payload<'a, T>(&'a Cell<u16>, T);
struct Owned<'a>(&'a Cell<u16>, [u64; 3]);
impl Drop for Owned<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() + (self.1[0] + self.1[1] + self.1[2]) as u16); }
}
impl<T> Drop for Payload<'_, T> { fn drop(&mut self) { self.0.set(999); } }
impl<T> AsyncDrop for Payload<'_, T> {
    async fn drop(self: Pin<&mut Self>) {
        self.0.set(7);
        let mut paused = false;
        poll_fn(|_| {
            if paused { Poll::Ready(()) } else { paused = true; Poll::Pending }
        }).await;
        self.0.set(11);
    }
}
fn main() {
    let events = Cell::new(0);
    let mut value = MaybeUninit::new(Payload(&events, Owned(&events, [13u64, 17, 19])));
    let mut dropping = pin!(unsafe { async_drop_in_place(value.as_mut_ptr()) });
    let mut context = Context::from_waker(Waker::noop());
    assert!(dropping.as_mut().poll(&mut context).is_pending());
    assert_eq!(events.get(), 7);
    assert!(dropping.as_mut().poll(&mut context).is_ready());
    assert_eq!(events.get(), 60);
}
