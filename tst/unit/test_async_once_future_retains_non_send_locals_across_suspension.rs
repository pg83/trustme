#![feature(specialization)]
#![allow(incomplete_features)]

use std::future::{pending, Future};
use std::rc::Rc;
use std::task::{Context, Poll, Waker};

trait SendFlag { fn flag() -> bool; }
impl<T> SendFlag for T { default fn flag() -> bool { false } }
impl<T: Send> SendFlag for T { fn flag() -> bool { true } }

fn send_flag<T>(_: &T) -> bool { T::flag() }
fn take<F: AsyncFnOnce() -> u16>(call: F) -> impl Future<Output = u16> { call() }

fn main() {
    let future = take(async || {
        let value = Rc::new(259u16);
        pending::<()>().await;
        *value
    });
    assert!(!send_flag(&future));
    let mut future = std::pin::pin!(future);
    assert!(matches!(future.as_mut().poll(&mut Context::from_waker(Waker::noop())), Poll::Pending));
    assert!(!send_flag(future.as_ref().get_ref()));
}
