//@ edition: 2021

#![feature(async_drop)]
#![allow(incomplete_features)]

use std::future::{async_drop_in_place, AsyncDrop, Future};
use std::mem::ManuallyDrop;
use std::pin::{pin, Pin};
use std::task::{Context, Poll, Waker};

struct Value(usize);

impl Drop for Value {
    fn drop(&mut self) {}
}

impl AsyncDrop for Value {
    async fn drop(self: Pin<&mut Self>) {
        assert!(self.0 == 7 || self.0 == 8);
    }
}

async fn hold_value() {
    let _value = Value(7);
    nested().await;
}

async fn nested() {
    let _value = Value(8);
}

fn main() {
    let mut held = ManuallyDrop::new(hold_value());
    let waker = Waker::noop();
    let mut context = Context::from_waker(waker);
    let mut held_pin = unsafe { Pin::new_unchecked(&mut *held) };
    assert_eq!(held_pin.as_mut().poll(&mut context), Poll::Ready(()));
    drop(held_pin);

    let mut dropping = pin!(unsafe { async_drop_in_place(&mut *held) });
    assert_eq!(dropping.as_mut().poll(&mut context), Poll::Ready(()));
}
