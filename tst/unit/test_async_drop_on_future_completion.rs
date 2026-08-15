#![feature(async_drop)]
#![allow(incomplete_features)]

use std::future::{AsyncDrop, Future};
use std::pin::{pin, Pin};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::task::{Context, Poll, Waker};

static SYNC_DROPS: AtomicUsize = AtomicUsize::new(0);
static ASYNC_DROPS: AtomicUsize = AtomicUsize::new(0);

struct Resource;

struct PendingOnce(bool);

impl Future for PendingOnce {
    type Output = ();

    fn poll(mut self: Pin<&mut Self>, context: &mut Context<'_>) -> Poll<()> {
        if self.0 {
            Poll::Ready(())
        } else {
            self.0 = true;
            context.waker().wake_by_ref();
            Poll::Pending
        }
    }
}

impl Drop for Resource {
    fn drop(&mut self) {
        SYNC_DROPS.fetch_add(1, Ordering::SeqCst);
    }
}

impl AsyncDrop for Resource {
    async fn drop(self: Pin<&mut Self>) {
        PendingOnce(false).await;
        ASYNC_DROPS.fetch_add(1, Ordering::SeqCst);
    }
}

async fn use_resource() {
    let _resource = Resource;
}

fn main() {
    let waker = Waker::noop();
    let mut context = Context::from_waker(&waker);
    let mut future = pin!(use_resource());

    assert_eq!(future.as_mut().poll(&mut context), Poll::Pending);
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(()));
    assert_eq!(ASYNC_DROPS.load(Ordering::SeqCst), 1);
    assert_eq!(SYNC_DROPS.load(Ordering::SeqCst), 0);
}
