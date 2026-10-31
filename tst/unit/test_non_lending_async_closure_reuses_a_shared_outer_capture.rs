use std::future::Future;
use std::task::{Context, Poll, Waker};

fn ready<F: Future<Output = u16>>(future: F) -> u16 {
    let mut future = std::pin::pin!(future);
    match future.as_mut().poll(&mut Context::from_waker(Waker::noop())) {
        Poll::Ready(value) => value,
        Poll::Pending => panic!("immediate closure suspended"),
    }
}

fn twice<F: FnMut(u8) -> R, R: Future<Output = u16>>(mut call: F) -> u16 {
    ready(call(3)) + ready(call(5))
}

fn main() {
    let word = String::from("rust");
    assert_eq!(twice(async |amount: u8| word.len() as u16 + u16::from(amount)), 16);
    let value = 257u16;
    let reference = &value;
    assert_eq!(twice(async move |amount: u8| *reference + u16::from(amount)), 522);
}
