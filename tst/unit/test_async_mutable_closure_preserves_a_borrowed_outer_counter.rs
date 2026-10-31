use std::future::Future;
use std::task::{Context, Poll, Waker};

fn ready<F: Future>(value: F) -> F::Output {
    let mut value = std::pin::pin!(value);
    match value.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(result) => result,
        Poll::Pending => panic!("immediate closure suspended"),
    }
}

fn main() {
    let mut total = 65536u32;
    {
        let mut call = async |amount: u16| {
            total += u32::from(amount);
            total
        };
        assert_eq!(ready(call(3)), 65539);
        assert_eq!(ready(call(5)), 65544);
    }
    assert_eq!(total, 65544);
}
