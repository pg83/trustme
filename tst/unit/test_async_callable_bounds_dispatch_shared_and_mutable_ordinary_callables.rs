use std::future::Future;
use std::task::{Context, Poll, Waker};

fn ready<F: Future<Output = u16>>(future: F) -> u16 {
    let mut future = std::pin::pin!(future);
    match future.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(value) => value,
        Poll::Pending => panic!("immediate callable suspended"),
    }
}

async fn answer(amount: u8) -> u16 { 257 + u16::from(amount) }
async fn shared<F: AsyncFn(u8) -> u16>(call: &F) -> u16 { call(3).await + call(5).await }
async fn mutable<F: AsyncFnMut(u8) -> u16>(mut call: F) -> u16 { call(3).await + call(5).await }

fn main() {
    assert_eq!(ready(shared(&answer)), 522);
    assert_eq!(ready(mutable(answer)), 522);
    let pointer: fn(u8) -> _ = answer;
    assert_eq!(ready(shared(&pointer)), 522);
    assert_eq!(ready(mutable(pointer)), 522);
    let offset = 257u16;
    let plain = move |amount: u8| async move { offset + u16::from(amount) };
    assert_eq!(ready(shared(&plain)), 522);
    assert_eq!(ready(mutable(plain)), 522);
    let mut total = 257u16;
    let accumulating = move |amount: u8| {
        total += u16::from(amount);
        async move { total }
    };
    assert_eq!(ready(mutable(accumulating)), 525);
}
