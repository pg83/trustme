#![feature(async_drop)]

use std::cell::Cell;
use std::future::{async_drop_in_place, Future};
use std::mem::MaybeUninit;
use std::pin::pin;
use std::task::{Context, Poll, Waker};

struct Field<'a>(&'a Cell<u32>, u32);

impl Drop for Field<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() * 10 + self.1);
    }
}

fn main() {
    let order = Cell::new(0);
    let mut storage = MaybeUninit::new((Field(&order, 1), Field(&order, 2)));
    let mut future = pin!(unsafe { async_drop_in_place(storage.as_mut_ptr()) });
    let mut context = Context::from_waker(Waker::noop());

    assert!(matches!(future.as_mut().poll(&mut context), Poll::Ready(())));
    assert_eq!(order.get(), 12);
}
