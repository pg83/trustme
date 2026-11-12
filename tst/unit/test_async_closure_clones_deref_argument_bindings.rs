#![feature(deref_patterns)]

use std::future::Future;
use std::task::{Context, Poll, Waker};

fn ready<F: Future>(future: F) -> F::Output {
    let mut future = std::pin::pin!(future);
    match future.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(value) => value,
        Poll::Pending => panic!("immediate closure suspended"),
    }
}

fn main() {
    let sum = async |deref!((ref first, ref last)): Box<(u16, u16)>| {
        *first + *last
    };
    assert_eq!(ready(sum(Box::new((17, 19)))), 36);
    assert_eq!(ready(sum(Box::new((23, 29)))), 52);
}
