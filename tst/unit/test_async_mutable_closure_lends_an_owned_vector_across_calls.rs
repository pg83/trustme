use std::future::Future;
use std::task::{Context, Poll, Waker};

fn make() -> impl AsyncFnMut(u8) -> usize {
    let mut values = Vec::new();
    async move |value| {
        values.push(value);
        values.len()
    }
}

fn ready<F: Future>(value: F) -> F::Output {
    let mut value = std::pin::pin!(value);
    match value.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(result) => result,
        Poll::Pending => panic!("immediate closure suspended"),
    }
}

fn main() {
    let mut call = make();
    assert_eq!(ready(call(7)), 1);
    assert_eq!(ready(call(11)), 2);
    assert_eq!(ready(call(13)), 3);
}
