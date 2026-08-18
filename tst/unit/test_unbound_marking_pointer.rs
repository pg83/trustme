// A binding that names an item can still be waiting for it: until then there
// are no markings to read, the same as for one that names none.
//@ edition: 2021
//@ crate-type: lib
//@ compile-flags: --emit=metadata

use std::future::Future;
use std::pin::Pin;

trait FutureExt: Future + Sized + Send + 'static {
    fn boxed(self) -> Pin<Box<dyn Future<Output = Self::Output> + Send + 'static>> {
        Box::pin(self)
    }
}

trait StreamExt: Future + Sized + Send + 'static {
    fn boxed(self) -> Pin<Box<dyn Future<Output = Self::Output> + Send + 'static>> {
        Box::pin(self)
    }
}

impl<T: Future + Sized + Send + 'static> FutureExt for T {}

fn spawn<T>(_: T) -> impl Future<Output = ()> {
    async {}
}

pub fn go(i: usize) -> impl Future<Output = ()> + Send + 'static {
    async move {
        if i != 0 {
            spawn(async move {
                let fut = go(i - 1).boxed();
                fut.await;
            })
            .await;
        }
    }
}
