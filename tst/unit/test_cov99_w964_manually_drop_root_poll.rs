//@ edition: 2021

#![feature(async_drop)]
#![allow(incomplete_features)]

use core::cell::Cell;
use core::future::{async_drop_in_place, Future};
use core::mem::ManuallyDrop;
use core::pin::pin;
use core::task::{Context, Poll, Waker};

struct Value<'a>(&'a Cell<u32>);

impl Drop for Value<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() + 1);
    }
}

fn main() {
    let drops = Cell::new(0);
    let mut value = ManuallyDrop::new(Value(&drops));
    {
        let mut dropping = pin!(unsafe { async_drop_in_place(&mut value) });
        let mut context = Context::from_waker(Waker::noop());
        assert_eq!(dropping.as_mut().poll(&mut context), Poll::Ready(()));
    }
    assert_eq!(drops.get(), 0);
    unsafe { ManuallyDrop::drop(&mut value) };
    assert_eq!(drops.get(), 1);
}
