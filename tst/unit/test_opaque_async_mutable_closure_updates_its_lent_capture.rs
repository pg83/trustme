use std::future::Future;
use std::task::{Context, Poll, Waker};

fn make(start: u32) -> impl AsyncFnMut(u16) -> u32 {
    let mut total = start;
    async move |amount| {
        total += u32::from(amount);
        total
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
    let mut call = make(65536);
    assert_eq!(ready(call(3)), 65539);
    assert_eq!(ready(call(5)), 65544);
}
