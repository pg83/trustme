use std::future::Future;
use std::task::{Context, Poll, Waker};

fn make() -> impl AsyncFn(u8) -> u16 {
    async |value| u16::from(value) + 257
}

fn ready<F: Future>(value: F) -> F::Output {
    let mut value = std::pin::pin!(value);
    match value.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(result) => result,
        Poll::Pending => panic!("immediate closure suspended"),
    }
}

fn main() {
    let call = make();
    assert_eq!(ready(call(0)), 257);
    assert_eq!(ready(call(255)), 512);
}
