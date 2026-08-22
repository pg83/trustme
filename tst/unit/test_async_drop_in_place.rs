//@ edition: 2021

#![feature(async_drop)]
#![allow(incomplete_features)]

use core::future::{async_drop_in_place, Future};
use core::mem::{size_of_val, MaybeUninit};
use core::pin::pin;
use core::sync::atomic::{AtomicUsize, Ordering};
use core::task::{Context, Poll, Waker};

static DROPS: AtomicUsize = AtomicUsize::new(0);

struct Value;

impl Drop for Value {
    fn drop(&mut self) {
        DROPS.fetch_add(1, Ordering::SeqCst);
    }
}

fn main() {
    let mut value = MaybeUninit::new(Value);
    let mut future = pin!(unsafe { async_drop_in_place(value.as_mut_ptr()) });
    assert_eq!(size_of_val(&*future), 16);

    let waker = Waker::noop();
    let mut context = Context::from_waker(waker);
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(()));
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(()));
    assert_eq!(DROPS.load(Ordering::SeqCst), 1);
}
