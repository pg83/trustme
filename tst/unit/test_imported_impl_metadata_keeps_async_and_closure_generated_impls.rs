//@ aux-build: impl_metadata_members.rs
//@ compile-variant: -O
use std::future::Future;
use std::task::{Context, Poll, Waker};
fn main() {
    let mut future = Box::pin(async { impl_metadata_members::next(5).await + 3 });
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(25));
    let mut callback = async |value| impl_metadata_members::next(value).await;
    let mut second = Box::pin(callback(7));
    assert_eq!(second.as_mut().poll(&mut context), Poll::Ready(24));
}
