// rustc checks a struct literal's field value with the field's type as its
// expectation, then coerces it (`check_expr_coercible_to_type`), and `coerce`
// starts with `resolve_vars_with_obligations`. For `Pin::from(Box::new(..))`
// into a `Pin<Box<dyn Future>>` field the expectation says nothing about
// `From`'s parameter, so the argument makes it `Box<TestFuture<F>>`; the
// pending `Pin<?P>: From<Box<TestFuture<F>>>` then has one impl, `?P` is
// `Box<TestFuture<F>>`, and only the result unsizes into the field.
// wasm-bindgen-test queues its tests this way.
use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll, RawWaker, RawWakerVTable, Waker};

struct TestFuture<F> {
    test: F,
}

impl<F: Future<Output = u8>> Future for TestFuture<F> {
    type Output = u8;
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<u8> {
        unsafe { self.map_unchecked_mut(|s| &mut s.test) }.poll(cx)
    }
}

struct Test {
    name: &'static str,
    future: Pin<Box<dyn Future<Output = u8>>>,
}

fn queue<F: Future<Output = u8> + 'static>(name: &'static str, test: F) -> Test {
    let future = TestFuture { test };
    Test { name, future: Pin::from(Box::new(future)) }
}

fn main() {
    static VTABLE: RawWakerVTable = RawWakerVTable::new(|p| RawWaker::new(p, &VTABLE), |_| {}, |_| {}, |_| {});
    let waker = unsafe { Waker::from_raw(RawWaker::new(std::ptr::null(), &VTABLE)) };
    let mut test = queue("seven", async { 7 });
    assert_eq!(test.name, "seven");
    assert_eq!(test.future.as_mut().poll(&mut Context::from_waker(&waker)), Poll::Ready(7));
}
