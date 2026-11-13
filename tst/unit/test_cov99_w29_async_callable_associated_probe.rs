use std::future::Future;
use std::marker::PhantomData;
use std::ops::AsyncFnOnce;
use std::pin::Pin;
use std::task::{Context, Poll};

trait Source {
    type Value;
}

struct Slot<T>(PhantomData<T>);

impl<T> Source for Slot<T> {
    type Value = T;
}

struct Waiting<S>(PhantomData<S>);

impl<S, U> Future for Waiting<S>
where
    S: Source<Value = Vec<U>>,
{
    type Output = (U,);

    fn poll(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<Self::Output> {
        Poll::Pending
    }
}

fn produce<T>() -> Waiting<Slot<T>> {
    Waiting(PhantomData)
}

struct Outer<F>(F);

trait Relay {
    type Value;
}

impl<F, U> Relay for Outer<F>
where
    F: AsyncFnOnce() -> U,
{
    type Value = (U,);
}

trait Accept<T> {}

impl<F> Accept<<Outer<F> as Relay>::Value> for Outer<F>
where
    Outer<F>: Relay,
{}

fn require<X: Accept<((u8,),)>>(_: &X) {}

fn main() {
    let value = Outer(produce::<_>);
    require(&value);
}
