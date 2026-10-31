use std::future::Future;
use std::task::{Context, Poll, Waker};

fn ready<F: Future>(value: F) -> F::Output {
    let mut value = std::pin::pin!(value);
    match value.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(result) => result,
        Poll::Pending => panic!("immediate future suspended"),
    }
}

fn twice<F, R>(mut call: F) -> u16
where F: FnMut(u8) -> R, R: Future<Output = u16> {
    ready(call(3)) + ready(call(5))
}

fn main() {
    assert_eq!(twice(async |value: u8| u16::from(value) + 257), 522);
}
