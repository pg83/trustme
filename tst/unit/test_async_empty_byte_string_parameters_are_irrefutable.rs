//@ compile-variant: -O
use std::future::Future;
use std::task::{Context, Poll, Waker};
async fn selected((b"",): (&[u8; 0],)) -> u32 { 127 }
fn main() {
    let bytes = [];
    let mut future = std::pin::pin!(selected((&bytes,)));
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(127));
}
