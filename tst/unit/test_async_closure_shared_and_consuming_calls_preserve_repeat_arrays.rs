use std::future::Future;
use std::task::{Context, Poll, Waker};

fn ready<F: Future<Output = u16>>(future: F) -> u16 {
    let mut future = std::pin::pin!(future);
    match future.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(value) => value,
        Poll::Pending => panic!("immediate closure suspended"),
    }
}

fn take<F: AsyncFnOnce(u8) -> u16>(call: F, amount: u8) -> impl Future<Output = u16> {
    call(amount)
}

fn main() {
    let bias = 257u16;
    let call = async move |amount: u8| {
        let values = [u16::from(amount); 3];
        bias + values[2]
    };
    assert_eq!(ready(call(3)), 260);
    assert_eq!(ready(take(call, 5)), 262);
}
