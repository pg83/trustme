//@ edition: 2021

#![feature(async_drop)]
#![allow(incomplete_features)]

use core::future::{AsyncDrop, Future};
use core::pin::{pin, Pin};
use core::task::{Context, Poll, Waker};

enum Value {
    A,
    B,
}

impl Drop for Value {
    fn drop(&mut self) {}
}

impl AsyncDrop for Value {
    async fn drop(mut self: Pin<&mut Self>) {
        let replacement = match &*self {
            Value::A => Value::B,
            Value::B => Value::A,
        };
        core::mem::forget(core::mem::replace(&mut *self, replacement));
    }
}

async fn finish() {
    let _value = Value::A;
}

fn main() {
    let waker = Waker::noop();
    let mut context = Context::from_waker(waker);
    let mut future = pin!(finish());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(()));
}
