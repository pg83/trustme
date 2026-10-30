#![feature(async_drop)]
#![allow(incomplete_features)]
use std::future::{poll_fn, AsyncDrop, Future};
use std::pin::{pin, Pin};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::task::{Context, Poll, Waker};
static ASYNC: AtomicUsize = AtomicUsize::new(0);
static SYNC: AtomicUsize = AtomicUsize::new(0);
struct Ticket(usize);
impl Drop for Ticket { fn drop(&mut self) { SYNC.fetch_add(1, Ordering::SeqCst); } }
impl AsyncDrop for Ticket {
    async fn drop(self: Pin<&mut Self>) {
        let mut ready = false;
        poll_fn(|context| {
            if ready { Poll::Ready(()) } else {
                ready = true;
                context.waker().wake_by_ref();
                Poll::Pending
            }
        }).await;
        ASYNC.fetch_add(self.0, Ordering::SeqCst);
    }
}
enum Payload { Empty, Small(u8), Full(Ticket) }
struct Envelope(Payload);
async fn hold(value: Envelope) { std::hint::black_box(&value); }
fn check(value: Payload, expected: usize) {
    ASYNC.store(0, Ordering::SeqCst);
    let mut future = pin!(hold(Envelope(value)));
    let mut context = Context::from_waker(Waker::noop());
    if expected != 0 {
        assert_eq!(future.as_mut().poll(&mut context), Poll::Pending);
        assert_eq!(ASYNC.load(Ordering::SeqCst), 0);
        assert_eq!(SYNC.load(Ordering::SeqCst), 0);
    }
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(()));
    assert_eq!(ASYNC.load(Ordering::SeqCst), expected);
    assert_eq!(SYNC.load(Ordering::SeqCst), 0);
}
fn main() {
    check(Payload::Empty, 0);
    check(Payload::Small(11), 0);
    check(Payload::Full(Ticket(17)), 17);
}
