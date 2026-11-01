#![feature(async_fn_traits, unboxed_closures)]
use core::future::{ready, Ready, Future};
use core::ops::AsyncFn;
use core::pin::Pin;
use core::task::{Context, Poll, Waker};
fn make(value: u8) -> Ready<u16> { ready(value as u16 + 5) }
fn main() {
    let pointer: fn(u8) -> Ready<u16> = make;
    let call = <fn(u8) -> Ready<u16> as AsyncFn<(u8,)>>::async_call;
    let mut future = call(&pointer, (11,));
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(Pin::new(&mut future).poll(&mut context), Poll::Ready(16));
}
