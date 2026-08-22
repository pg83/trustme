use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};

struct Pair {
    value: usize,
    reference: &'static u32,
}

#[inline(never)]
fn nop<T>(_: T) {}

fn main() {
    let mut future = pin!(async {
        let pair = Pair {
            reference: &42,
            value: async { 0 }.await,
        };

        assert_eq!(pair.value, 0);
        nop(&pair.reference);
        assert_ne!(unsafe { std::mem::transmute::<&u32, usize>(pair.reference) }, 0);
        async {}.await;
    });
    let waker = Waker::noop();
    let mut context = Context::from_waker(waker);
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(()));
}
