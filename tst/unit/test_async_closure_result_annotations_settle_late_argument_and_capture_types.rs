use std::future::Future;
use std::task::{Context, Poll, Waker};

fn ready<F: Future>(future: F) -> F::Output {
    let mut future = std::pin::pin!(future);
    match future.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(value) => value,
        Poll::Pending => panic!("immediate future suspended"),
    }
}

fn choose(captured: bool) -> u16 {
    let saved = if captured { Some(641) } else { None };
    let call = async move |fallback| saved.unwrap_or(fallback);
    let future = call(643);
    let result = ready(future);
    let result: u16 = result;
    result
}

fn main() {
    assert_eq!(choose(true), 641);
    assert_eq!(choose(false), 643);
}
