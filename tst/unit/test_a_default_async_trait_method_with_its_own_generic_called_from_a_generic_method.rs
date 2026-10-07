//@ edition: 2021
// tor-proto (arti): `ChannelBaseHandshake<T>` has a default
// `async fn send_versions_cell<F>(&mut self, now_fn: F)`, and
// `ClientInitiatorHandshake<T, S>::connect<F>` awaits
// `self.send_versions_cell(now_fn)`.
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};

trait Base<T> {
    fn value(&self) -> u32;

    async fn send<F>(&mut self, now: F) -> u32
    where
        F: FnOnce() -> u32,
    {
        self.value() + now()
    }
}

struct Handshake<T, S>(T, S);

impl<T: Copy + Into<u32>, S> Base<T> for Handshake<T, S> {
    fn value(&self) -> u32 {
        self.0.into()
    }
}

impl<T: Copy + Into<u32>, S> Handshake<T, S> {
    async fn connect<F>(mut self, now: F) -> u32
    where
        F: FnOnce() -> u32,
    {
        self.send(now).await
    }
}

fn main() {
    let mut future = pin!(Handshake(2u8, ()).connect(|| 5));
    let mut cx = Context::from_waker(Waker::noop());
    assert_eq!(future.as_mut().poll(&mut cx), Poll::Ready(7));
}
