use std::future::Future;
use std::task::{Context, Poll, Waker};

fn take<F: AsyncFnOnce(u8) -> u16>(call: F, value: u8) -> impl Future<Output = u16> {
    call(value)
}

fn async_source() -> u16 {
    let call = |value: u8| async move { u16::from(value) + 257 };
    let mut future = std::pin::pin!(take(call, 7));
    match future.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(result) => result,
        Poll::Pending => panic!("immediate future suspended"),
    }
}

fn main() { assert_eq!(async_source(), 264); }
