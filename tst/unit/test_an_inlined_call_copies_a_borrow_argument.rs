//@ compile-flags: -O
// `await` builds its `poll` call with a borrow as the argument itself - a
// `MIRParam::Borrow`, not an lvalue - and the inliner copies an argument it
// cannot address into a local before it clones the callee's body.
use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll, RawWaker, RawWakerVTable, Waker};

struct Ready(u32);

impl Future for Ready {
    type Output = u32;
    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<u32> {
        Poll::Ready(self.0)
    }
}

async fn run() -> u32 {
    Ready(7).await
}

static VTABLE: RawWakerVTable = RawWakerVTable::new(
    |p| RawWaker::new(p, &VTABLE),
    |_| {},
    |_| {},
    |_| {},
);

fn main() {
    let waker = unsafe { Waker::from_raw(RawWaker::new(std::ptr::null(), &VTABLE)) };
    let mut context = Context::from_waker(&waker);
    let mut future = Box::pin(run());
    match future.as_mut().poll(&mut context) {
        Poll::Ready(value) => assert_eq!(value, 7),
        Poll::Pending => panic!("the future was ready"),
    }
}
