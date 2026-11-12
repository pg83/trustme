use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll, Waker};

fn main() {
    let captured = String::from("nested");
    let future = &mut async move { captured.len() as u16 + 1000 };
    let mut future = unsafe { Pin::new_unchecked(future) };
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(1006));
}
