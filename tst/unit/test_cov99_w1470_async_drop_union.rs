#![feature(async_drop)]
#![allow(incomplete_features)]

use std::future::{AsyncDrop, Future};
use std::pin::{pin, Pin};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::task::{Context, Poll, Waker};

static DROPS: AtomicUsize = AtomicUsize::new(0);

union AsyncUnion {
    value: usize,
}

impl Drop for AsyncUnion {
    fn drop(&mut self) {
        DROPS.fetch_add(100, Ordering::Relaxed);
    }
}

impl AsyncDrop for AsyncUnion {
    async fn drop(self: Pin<&mut Self>) {
        DROPS.fetch_add(unsafe { self.value }, Ordering::Relaxed);
    }
}

async fn scope() {
    let _value = AsyncUnion { value: 7 };
}

fn main() {
    {
        let mut future = pin!(scope());
        let mut context = Context::from_waker(Waker::noop());
        assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(()));
    }
    assert_eq!(DROPS.load(Ordering::Relaxed), 7);
}
