use std::future::Future;
use std::task::{Context, Poll, Waker};

macro_rules! make {
    ($initial:expr) => {{
        let mut total = $initial;
        async move |amount: u16| {
            total += u32::from(amount);
            total
        }
    }};
}

fn ready<F: Future>(value: F) -> F::Output {
    let mut value = std::pin::pin!(value);
    match value.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(result) => result,
        Poll::Pending => panic!("immediate closure suspended"),
    }
}

fn main() {
    let mut call = make!(65536u32);
    assert_eq!(ready(call(3)), 65539);
    assert_eq!(ready(call(5)), 65544);
}
