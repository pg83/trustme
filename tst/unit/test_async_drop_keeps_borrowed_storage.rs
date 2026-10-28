//@ compile-flags: -Zmir-opt-level=2
#![feature(async_drop)]
#![allow(incomplete_features)]

use std::future::{AsyncDrop, Future};
use std::pin::{pin, Pin};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::task::{Context, Poll, Waker};

static EVENTS: AtomicUsize = AtomicUsize::new(0);

struct Resource(u8);

impl Drop for Resource {
    fn drop(&mut self) { EVENTS.fetch_add(100, Ordering::SeqCst); }
}

impl AsyncDrop for Resource {
    async fn drop(self: Pin<&mut Self>) {
        let mut first = true;
        std::future::poll_fn(|cx| {
            if first {
                first = false;
                cx.waker().wake_by_ref();
                Poll::Pending
            } else {
                Poll::Ready(())
            }
        }).await;
        EVENTS.fetch_add(self.0 as usize, Ordering::SeqCst);
    }
}

async fn outer() {
    let value = Resource(7);
    let _nested = async move {
        std::hint::black_box(&value);
        std::future::pending::<()>().await;
    };
}

async fn take_box(take: bool) {
    let boxed = Box::new(Resource(3));
    if take {
        let _moved = *boxed;
    }
    let mut pause = true;
    std::future::poll_fn(|cx| {
        if pause {
            pause = false;
            cx.waker().wake_by_ref();
            Poll::Pending
        } else {
            Poll::Ready(())
        }
    }).await;
}

fn main() {
    let mut context = Context::from_waker(Waker::noop());
    let mut outer = pin!(outer());
    assert_eq!(outer.as_mut().poll(&mut context), Poll::Pending);
    assert_eq!(EVENTS.load(Ordering::SeqCst), 0);
    assert_eq!(outer.as_mut().poll(&mut context), Poll::Ready(()));
    assert_eq!(EVENTS.load(Ordering::SeqCst), 7);
    for (take, before) in [(true, 7), (false, 10)] {
        let mut future = pin!(take_box(take));
        assert_eq!(future.as_mut().poll(&mut context), Poll::Pending);
        assert_eq!(EVENTS.load(Ordering::SeqCst), before);
        let mut complete = false;
        for _ in 0..8 {
            let poll = future.as_mut().poll(&mut context);
            if poll == Poll::Ready(()) { complete = true; break; }
        }
        assert!(complete);
        assert_eq!(EVENTS.load(Ordering::SeqCst), before + 3);
    }
    assert_eq!(EVENTS.load(Ordering::SeqCst), 13);
}
