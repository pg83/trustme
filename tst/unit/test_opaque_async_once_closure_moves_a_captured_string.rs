use std::future::Future;
use std::task::{Context, Poll, Waker};

fn make(value: String) -> impl AsyncFnOnce(u8) -> (String, u16) {
    async move |tag| (value, u16::from(tag) + 257)
}

fn ready<F: Future>(value: F) -> F::Output {
    let mut value = std::pin::pin!(value);
    match value.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(result) => result,
        Poll::Pending => panic!("immediate closure suspended"),
    }
}

fn main() {
    let call = make(String::from("owned"));
    assert_eq!(ready(call(7)), (String::from("owned"), 264));
}
