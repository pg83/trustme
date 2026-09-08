/* A head equality that holds only by binding something is a question, not a
   difference of spelling: `spawn`'s `Fut: Future<Output = ()>` is met by
   `Remote<Fut>`, whose impl gives `()`, while the `Handle<T>` of the same
   destructuring gives `T`.  Matching either impl leaves an equality pending, and
   only normalization of both sides to one type may settle a head; taking a binding
   for an answer picked `Handle<Fut::Output>` here and asked `() = Fut::Output`
   (futures-util's `SpawnExt::spawn_with_handle`, combine). */

use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll};

struct Remote<F: Future> {
    inner: F,
}

impl<F: Future> Future for Remote<F> {
    type Output = ();
    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<()> {
        Poll::Pending
    }
}

struct Handle<T> {
    value: Option<T>,
}

impl<T> Future for Handle<T> {
    type Output = T;
    fn poll(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<T> {
        Poll::Pending
    }
}

fn remote_handle<F: Future>(f: F) -> (Remote<F>, Handle<F::Output>) {
    (Remote { inner: f }, Handle { value: None })
}

trait Spawn {
    fn spawn<Fut>(&self, future: Fut) -> Result<(), ()>
    where
        Fut: Future<Output = ()> + Send + 'static;

    fn spawn_with_handle<Fut>(&self, future: Fut) -> Result<Handle<Fut::Output>, ()>
    where
        Fut: Future + Send + 'static,
        Fut::Output: Send,
    {
        let (future, handle) = remote_handle(future);
        self.spawn(future)?;
        Ok(handle)
    }
}

struct Pool;

impl Spawn for Pool {
    fn spawn<Fut>(&self, _future: Fut) -> Result<(), ()>
    where
        Fut: Future<Output = ()> + Send + 'static,
    {
        Ok(())
    }
}

async fn one() -> u32 {
    1
}

fn main() {
    let pool = Pool;
    let handle = pool.spawn_with_handle(one()).unwrap();
    assert!(handle.value.is_none());
}
