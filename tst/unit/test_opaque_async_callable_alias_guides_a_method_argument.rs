#![feature(type_alias_impl_trait)]

use std::future::Future;
use std::task::{Context, Poll, Waker};

type Callback = impl AsyncFnMut(u16) -> u32;
struct Factory;

impl Factory {
    fn keep(&self, callback: Callback) -> Callback { callback }

    #[define_opaque(Callback)]
    fn make(&self) -> Callback {
        let mut total = 65537;
        self.keep(async move |amount| { total += u32::from(amount); total })
    }
}

fn ready<F: Future<Output = u32>>(future: F) -> u32 {
    let mut future = std::pin::pin!(future);
    match future.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(value) => value,
        Poll::Pending => panic!("immediate closure suspended"),
    }
}

fn main() {
    let mut callback = Factory.make();
    assert_eq!(ready(callback(257)), 65794);
    assert_eq!(ready(callback(3)), 65797);
}
