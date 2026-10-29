//@ edition: 2021
#![feature(async_drop)]
#![allow(incomplete_features)]
use std::future::{async_drop_in_place, AsyncDrop, Future};
use std::mem::MaybeUninit;
use std::pin::{pin, Pin};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::task::{Context, Poll, Waker};

static ORDER: AtomicUsize = AtomicUsize::new(0);
static SYNC: AtomicUsize = AtomicUsize::new(0);
struct Digit(usize);
impl Drop for Digit {
    fn drop(&mut self) { SYNC.fetch_add(1, Ordering::SeqCst); }
}
impl AsyncDrop for Digit {
    async fn drop(self: Pin<&mut Self>) {
        let mut waiting = true;
        std::future::poll_fn(|cx| {
            if waiting {
                waiting = false;
                cx.waker().wake_by_ref();
                Poll::Pending
            } else { Poll::Ready(()) }
        }).await;
        ORDER.store(ORDER.load(Ordering::SeqCst) * 10 + self.0, Ordering::SeqCst);
    }
}
fn discard<const N: usize>(value: [Digit; N], expected: usize) {
    ORDER.store(0, Ordering::SeqCst);
    let mut storage = MaybeUninit::new(value);
    let mut future = pin!(unsafe { async_drop_in_place(storage.as_mut_ptr()) });
    let mut context = Context::from_waker(Waker::noop());
    for _ in 0..N { assert_eq!(future.as_mut().poll(&mut context), Poll::Pending); }
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(()));
    assert_eq!(ORDER.load(Ordering::SeqCst), expected);
    assert_eq!(SYNC.load(Ordering::SeqCst), 0);
}
fn main() {
    discard([], 0);
    discard([Digit(4)], 4);
    discard([Digit(2), Digit(7), Digit(3)], 273);
}
