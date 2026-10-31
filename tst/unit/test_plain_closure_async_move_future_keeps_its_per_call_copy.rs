use std::future::Future;
use std::task::{Context, Poll, Waker};

fn ready<F: Future>(value: F) -> F::Output {
    let mut value = std::pin::pin!(value);
    match value.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(result) => result,
        Poll::Pending => panic!("immediate future suspended"),
    }
}

fn main() {
    let start = 65536u32;
    let call = move |amount: u16| async move {
        let mut total = start;
        total += u32::from(amount);
        total
    };
    assert_eq!(ready(call(3)), 65539);
    assert_eq!(ready(call(5)), 65541);
}
