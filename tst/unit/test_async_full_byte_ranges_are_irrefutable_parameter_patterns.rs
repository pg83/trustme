//@ compile-variant: -O
use std::future::Future;
use std::task::{Context, Poll, Waker};
async fn selected((0..=255,): (u8,)) -> u32 { 131 }
fn main() {
    let mut context = Context::from_waker(Waker::noop());
    let mut low = std::pin::pin!(selected((0,)));
    let mut high = std::pin::pin!(selected((255,)));
    assert_eq!(low.as_mut().poll(&mut context), Poll::Ready(131));
    assert_eq!(high.as_mut().poll(&mut context), Poll::Ready(131));
}
