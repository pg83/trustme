#![feature(async_drop)]
#![allow(incomplete_features)]
use std::future::{AsyncDrop, Future};
use std::pin::{pin, Pin};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::task::{Context, Poll, Waker};
static ORDER: AtomicUsize = AtomicUsize::new(0);
fn record(value: usize) { ORDER.store(ORDER.load(Ordering::SeqCst) * 10 + value, Ordering::SeqCst); }
struct Ticket(usize);
impl Drop for Ticket { fn drop(&mut self) { record(1); } }
impl AsyncDrop for Ticket { async fn drop(self: Pin<&mut Self>) { record(self.0); } }
enum WithAsync { Empty, Full(Ticket) }
impl Drop for WithAsync { fn drop(&mut self) { record(8); } }
impl AsyncDrop for WithAsync { async fn drop(self: Pin<&mut Self>) { record(9); } }
enum WithSync { Empty, Full(Ticket) }
impl Drop for WithSync { fn drop(&mut self) { record(2); } }
fn finish<F: Future<Output = ()>>(future: F, expected: usize) {
    let mut future = pin!(future);
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(()));
    assert_eq!(ORDER.swap(0, Ordering::SeqCst), expected);
}
fn main() {
    finish(async { let value = WithAsync::Full(Ticket(4)); std::hint::black_box(&value); }, 94);
    finish(async { let value = WithAsync::Empty; std::hint::black_box(&value); }, 9);
    finish(async { let value = WithSync::Full(Ticket(7)); std::hint::black_box(&value); }, 27);
    finish(async { let value = WithSync::Empty; std::hint::black_box(&value); }, 2);
}
