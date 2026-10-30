//@ aux-build: opaque_codegen_shapes.rs
use opaque_codegen_shapes::{DefaultSequence, Maker};
use std::future::Future;
use std::task::{Context, Poll, Waker};
#[inline(never)]
fn local<const N: usize>(value: u8) -> impl Iterator<Item = u8> + Clone { [value; N].into_iter() }
fn check<I: Iterator<Item = u8> + Clone>(values: I, expected: &[u8]) {
    assert_eq!(values.clone().collect::<Vec<_>>(), expected);
    assert_eq!(values.count(), expected.len());
}
fn main() {
    check(local::<3>(2), &[2, 2, 2]);
    check(opaque_codegen_shapes::sequence::<2>(3), &[3, 3]);
    let maker = Maker(5);
    check(maker.sequence::<4>(), &[5, 5, 5, 5]);
    check(<Maker as DefaultSequence>::defaults(7), &[7, 8]);
    let mut future = std::pin::pin!(opaque_codegen_shapes::asynchronous(11));
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(12));
}
