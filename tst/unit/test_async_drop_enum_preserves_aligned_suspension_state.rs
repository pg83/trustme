//@ compile-flags: -Copt-level=2
#![feature(async_drop)]
#![allow(incomplete_features)]
use std::future::{async_drop_in_place, poll_fn, AsyncDrop, Future};
use std::mem::{align_of_val, size_of_val, MaybeUninit};
use std::pin::{pin, Pin};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::task::{Context, Poll, Waker};
static ORDER: AtomicUsize = AtomicUsize::new(0);
static SYNC: AtomicUsize = AtomicUsize::new(0);
#[repr(align(64))] struct Pad<const N: usize>([u8; N]);
async fn record<const N: usize>(id: usize) {
    let pad = std::hint::black_box(Pad([id as u8; N]));
    let mut waiting = true;
    poll_fn(|cx| {
        if waiting { waiting = false; cx.waker().wake_by_ref(); Poll::Pending }
        else { Poll::Ready(()) }
    }).await;
    assert_eq!(std::hint::black_box(&pad).0[N - 1], id as u8);
    ORDER.store(ORDER.load(Ordering::SeqCst) * 10 + id, Ordering::SeqCst);
}
struct Ticket<const N: usize>(usize);
impl<const N: usize> Drop for Ticket<N> { fn drop(&mut self) { SYNC.fetch_add(1, Ordering::SeqCst); } }
impl<const N: usize> AsyncDrop for Ticket<N> { async fn drop(self: Pin<&mut Self>) { record::<N>(self.0).await; } }
enum Plain { Empty, One(Ticket<97>), Pair { left: Ticket<9>, right: Ticket<97> } }
enum Custom { Empty, One(Ticket<97>), Pair(Ticket<9>, Ticket<97>) }
impl Drop for Custom { fn drop(&mut self) { SYNC.fetch_add(100, Ordering::SeqCst); } }
impl AsyncDrop for Custom { async fn drop(self: Pin<&mut Self>) { record::<17>(9).await; } }
fn check<T>(value: T, pending: usize, expected: usize) {
    ORDER.store(0, Ordering::SeqCst);
    let mut storage = MaybeUninit::new(value);
    let future = unsafe { async_drop_in_place(storage.as_mut_ptr()) };
    assert!(align_of_val(&future) >= 64);
    assert!(size_of_val(&future) >= 97);
    let mut future = pin!(future);
    let mut cx = Context::from_waker(Waker::noop());
    for _ in 0..pending { assert_eq!(future.as_mut().poll(&mut cx), Poll::Pending); }
    assert_eq!(future.as_mut().poll(&mut cx), Poll::Ready(()));
    assert_eq!(ORDER.load(Ordering::SeqCst), expected);
    assert_eq!(SYNC.load(Ordering::SeqCst), 0);
}
fn main() {
    check(Plain::Empty, 0, 0);
    check(Plain::One(Ticket(1)), 1, 1);
    check(Plain::Pair { left: Ticket(2), right: Ticket(3) }, 2, 23);
    check(Custom::Empty, 1, 9);
    check(Custom::One(Ticket(1)), 2, 91);
    check(Custom::Pair(Ticket(2), Ticket(3)), 3, 923);
}
