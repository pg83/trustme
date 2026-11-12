#![feature(async_fn_traits, unboxed_closures, tuple_trait)]

use std::future::Future;

trait Signature {
    type Args;
}

struct Unary;

impl Signature for Unary {
    type Args = (u16,);
}

struct Handler<S>(u16, std::marker::PhantomData<S>);

impl<S: Signature> AsyncFnOnce<S::Args> for Handler<S>
where
    S::Args: std::marker::Tuple,
{
    type Output = u16;
    type CallOnceFuture = std::future::Ready<u16>;

    extern "rust-call" fn async_call_once(self, _: S::Args) -> Self::CallOnceFuture {
        std::future::ready(self.0)
    }
}

fn main() {
    let handler = Handler::<Unary>(263, std::marker::PhantomData);
    let mut future = handler(269u16);
    let mut context = std::task::Context::from_waker(std::task::Waker::noop());
    assert_eq!(std::pin::Pin::new(&mut future).poll(&mut context), std::task::Poll::Ready(263));
}
