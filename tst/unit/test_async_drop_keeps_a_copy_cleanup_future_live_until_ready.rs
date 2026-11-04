//@ compile-variant: -O
#![feature(async_drop)]
#![allow(incomplete_features, refining_impl_trait)]
use std::future::{AsyncDrop, Future};
use std::pin::{pin, Pin};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::task::{Context, Poll, Waker};

static ORDER: AtomicUsize = AtomicUsize::new(0);
static SYNC: AtomicUsize = AtomicUsize::new(0);
#[derive(Clone, Copy)]
struct Cleanup { digit: usize, waiting: bool }
impl Future for Cleanup {
    type Output = ();
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<()> {
        if self.waiting {
            self.waiting = false;
            cx.waker().wake_by_ref();
            Poll::Pending
        } else {
            ORDER.store(ORDER.load(Ordering::SeqCst) * 10 + self.digit, Ordering::SeqCst);
            Poll::Ready(())
        }
    }
}
struct Job(usize);
impl Drop for Job {
    fn drop(&mut self) { SYNC.fetch_add(1, Ordering::SeqCst); }
}
impl AsyncDrop for Job {
    fn drop(self: Pin<&mut Self>) -> Cleanup { Cleanup { digit: self.0, waiting: true } }
}
async fn work() {
    let _first = Job(2);
    let _second = Job(5);
}
fn main() {
    let mut future = pin!(work());
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Pending);
    assert_eq!(ORDER.load(Ordering::SeqCst), 0);
    assert_eq!(future.as_mut().poll(&mut context), Poll::Pending);
    assert_eq!(ORDER.load(Ordering::SeqCst), 5);
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(()));
    assert_eq!(ORDER.load(Ordering::SeqCst), 52);
    assert_eq!(SYNC.load(Ordering::SeqCst), 0);
}
