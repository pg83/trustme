#![feature(deref_patterns)]

use std::future::Future;
use std::task::{Context, Poll, Waker};

async fn sum(deref!((ref first, ref last)): Box<(u16, u16)>) -> u16 {
    *first + *last
}

fn main() {
    let mut future = std::pin::pin!(sum(Box::new((61, 67))));
    match future.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(value) => assert_eq!(value, 128),
        Poll::Pending => panic!("immediate function suspended"),
    }
}
