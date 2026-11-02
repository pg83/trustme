//@ compile-variant: -O
#![feature(ergonomic_clones)]
#![allow(incomplete_features)]
use std::clone::UseCloned;
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};
#[derive(Clone)]
struct Token(u16);
impl UseCloned for Token {}
fn main() {
    let token = Token(17);
    assert!({
        let future = async use { token.0 + 6 };
        let mut future = pin!(future);
        let mut context = Context::from_waker(Waker::noop());
        matches!(future.as_mut().poll(&mut context), Poll::Ready(23))
    });
    assert_eq!(token.0, 17);
}
