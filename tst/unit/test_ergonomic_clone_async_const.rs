#![feature(ergonomic_clones)]
#![feature(const_closures)]
#![allow(incomplete_features)]

use std::clone::UseCloned;
use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll, RawWaker, RawWakerVTable, Waker};

#[derive(Clone)]
struct Token(u32);

impl UseCloned for Token {}

fn raw_waker() -> RawWaker {
    RawWaker::new(std::ptr::null(), &VTABLE)
}

static VTABLE: RawWakerVTable = RawWakerVTable::new(|_| raw_waker(), |_| {}, |_| {}, |_| {});

fn block_on<F: Future>(future: F) -> F::Output {
    let waker = unsafe { Waker::from_raw(raw_waker()) };
    let mut cx = Context::from_waker(&waker);
    let mut future = future;
    let mut future = unsafe { Pin::new_unchecked(&mut future) };
    loop {
        if let Poll::Ready(value) = future.as_mut().poll(&mut cx) {
            return value;
        }
    }
}

// A `const` closure is only allowed in a constant context, and nothing calls
// these: writing them down is the whole test.
const fn const_closures() {
    let _cloning = const use || {};
    let _plain = const || {};
}

fn main() {
    let token = Token(7);

    // `async use` captures by cloning what it cannot copy, so the original
    // stays usable after the future has taken it.
    let future = async use { token.0 };
    assert_eq!(token.0, 7);
    assert_eq!(block_on(future), 7);

    const_closures();
}
