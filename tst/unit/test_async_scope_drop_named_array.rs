#![feature(async_drop)]
use std::cell::Cell;
use std::future::{AsyncDrop, Future};
use std::pin::{pin, Pin};
use std::task::{Context, Poll, Waker};
struct Step<'a>(&'a Cell<u32>, u32);
impl Drop for Step<'_> { fn drop(&mut self) { self.0.set(9999); } }
impl AsyncDrop for Step<'_> {
    async fn drop(self: Pin<&mut Self>) {
        let mut paused = false;
        std::future::poll_fn(|_| {
            if paused { Poll::Ready(()) } else { paused = true; Poll::Pending }
        }).await;
        self.0.set(self.0.get() * 10 + self.1);
    }
}
struct Group<'a> { first: Step<'a>, rest: [Step<'a>; 2] }
async fn release(log: &Cell<u32>) {
    let values = Group { first: Step(log, 2), rest: [Step(log, 4), Step(log, 7)] };
    std::hint::black_box(&values);
}
fn main() {
    let log = Cell::new(0);
    let mut future = pin!(release(&log));
    let mut context = Context::from_waker(Waker::noop());
    for expected in [0, 2, 24] {
        assert!(future.as_mut().poll(&mut context).is_pending());
        assert_eq!(log.get(), expected);
    }
    assert!(future.as_mut().poll(&mut context).is_ready());
    assert_eq!(log.get(), 247);
}
