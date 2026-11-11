use std::future::Future;
use std::ops::AsyncFnOnce;
use std::pin::pin;
use std::task::{Context, Poll, Waker};

trait Completion {
    type Value;
}

struct Invocation<F>(F);

impl<F, T> Completion for Invocation<F>
where
    F: AsyncFnOnce() -> T,
{
    type Value = T;
}

trait Message {
    type Payload;
}

struct Deferred<F>(F);

impl<F> Message for Deferred<F>
where
    Invocation<F>: Completion,
{
    type Payload = <Invocation<F> as Completion>::Value;
}

fn inspect<F>(message: Deferred<F>) -> Deferred<F>
where
    Deferred<F>: Message<Payload = [u8; 3]>,
{
    message
}

fn main() {
    let Deferred(call) = inspect(Deferred(async || [Default::default(); 3]));
    let mut future = pin!(call());
    let mut context = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut context), Poll::Ready([0, 0, 0]));
}
