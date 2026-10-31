use std::future::Future;
use std::task::{Context, Poll, Waker};

async fn answer(value: u8) -> u16 { u16::from(value) + 257 }

fn take<F: AsyncFnOnce(u8) -> u16>(call: F, value: u8) -> impl Future<Output = u16> {
    call(value)
}

fn ready<F: Future>(value: F) -> F::Output {
    let mut value = std::pin::pin!(value);
    match value.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(result) => result,
        Poll::Pending => panic!("immediate future suspended"),
    }
}

fn main() {
    assert_eq!(ready(take(answer, 3)), 260);
    let pointer: fn(u8) -> _ = answer;
    assert_eq!(ready(take(pointer, 5)), 262);
    let call = |value: u8| async move { u16::from(value) + 263 };
    assert_eq!(ready(take(call, 7)), 270);
}
