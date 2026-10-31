#![feature(specialization)]
#![allow(incomplete_features)]

use std::cell::Cell;
use std::future::Future;
use std::pin::Pin;
use std::sync::{Arc, atomic::{AtomicUsize, Ordering}};
use std::task::{Context, Poll, Waker};

trait SendFlag { fn flag() -> bool; }
impl<T> SendFlag for T { default fn flag() -> bool { false } }
impl<T: Send> SendFlag for T { fn flag() -> bool { true } }

fn send_flag<T>(_: &T) -> bool { T::flag() }
fn take<F: AsyncFnOnce() -> u16>(call: F) -> impl Future<Output = u16> { call() }

struct Pause(bool);
impl Future for Pause {
    type Output = ();
    fn poll(mut self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<()> {
        if self.0 { Poll::Ready(()) } else { self.0 = true; Poll::Pending }
    }
}

struct Token { value: Cell<u16>, drops: Arc<AtomicUsize> }
impl Token { fn read(&self) -> u16 { self.value.get() } }
impl Drop for Token {
    fn drop(&mut self) { self.drops.fetch_add(1, Ordering::SeqCst); }
}

fn main() {
    let drops = Arc::new(AtomicUsize::new(0));
    let token = Token { value: Cell::new(263), drops: drops.clone() };
    let future = take(async move || { Pause(false).await; token.read() });
    assert!(send_flag(&future));
    assert_eq!(drops.load(Ordering::SeqCst), 0);
    {
        let mut future = std::pin::pin!(future);
        let mut context = Context::from_waker(Waker::noop());
        assert!(matches!(future.as_mut().poll(&mut context), Poll::Pending));
        assert!(send_flag(future.as_ref().get_ref()));
        assert_eq!(drops.load(Ordering::SeqCst), 0);
        assert!(matches!(future.as_mut().poll(&mut context), Poll::Ready(263)));
    }
    assert_eq!(drops.load(Ordering::SeqCst), 1);
}
