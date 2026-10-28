//@ edition: 2021
#![feature(async_drop)]
#![allow(incomplete_features)]

use std::future::{async_drop_in_place, AsyncDrop, Future};
use std::mem::MaybeUninit;
use std::pin::{pin, Pin};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::task::{Context, Poll, Waker};

static ASYNC: AtomicUsize = AtomicUsize::new(0);
static SYNC: AtomicUsize = AtomicUsize::new(0);

struct Resource(usize);
struct Pause(bool);

impl Future for Pause {
    type Output = ();
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<()> {
        if self.0 { Poll::Ready(()) } else {
            self.0 = true;
            cx.waker().wake_by_ref();
            Poll::Pending
        }
    }
}

impl Drop for Resource {
    fn drop(&mut self) { SYNC.fetch_add(self.0, Ordering::SeqCst); }
}
impl AsyncDrop for Resource {
    async fn drop(self: Pin<&mut Self>) {
        Pause(false).await;
        ASYNC.fetch_add(self.0, Ordering::SeqCst);
    }
}

enum Payload {
    Empty,
    One(Resource),
    Pair(Resource, Resource),
    Named { padding: [u64; 8], value: Resource },
}

fn check(value: Payload, pauses: usize, total: usize) {
    ASYNC.store(0, Ordering::SeqCst);
    let mut storage = MaybeUninit::new(value);
    let mut future = pin!(unsafe { async_drop_in_place(storage.as_mut_ptr()) });
    let mut cx = Context::from_waker(Waker::noop());
    for _ in 0..pauses {
        assert_eq!(future.as_mut().poll(&mut cx), Poll::Pending);
    }
    assert_eq!(future.as_mut().poll(&mut cx), Poll::Ready(()));
    assert_eq!(ASYNC.load(Ordering::SeqCst), total);
    assert_eq!(SYNC.load(Ordering::SeqCst), 0);
}

fn main() {
    check(Payload::Empty, 0, 0);
    check(Payload::One(Resource(3)), 1, 3);
    check(Payload::Pair(Resource(5), Resource(7)), 2, 12);
    check(Payload::Named { padding: [11; 8], value: Resource(13) }, 1, 13);
}
