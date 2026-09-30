// futures-util's `impl<Si, Item, U, Fut, F, E> Sink<U> for With<..> where
// E: From<Si::Error>` begins `poll_ready` with `ready!(self.poll(cx))?;`,
// whose residual is `E`: `E: From<E>` is the reflexive impl. Its `T` is an
// existential of the impl's parameters, which a rigid projection like
// `Si::Error` is distinct from - rustc fixes such parameters from the goal
// before any nested obligation is asked. 1de975d6c let every non-impl-scope
// existential defer against a projection, where only the unnamed parameters
// of a trait searched for `<T as _>::item` should, and read `E` as
// `Si::Error`.
use std::marker::PhantomData;
use std::task::{ready, Poll};

pub trait Sink<Item> {
    type Error;
    fn poll_ready(&mut self) -> Poll<Result<(), Self::Error>>;
}

pub struct With<Si, Item, E> {
    sink: Si,
    pending: Option<Result<Item, E>>,
    _marker: PhantomData<E>,
}

impl<Si, Item, E> With<Si, Item, E>
where
    Si: Sink<Item>,
    E: From<Si::Error>,
{
    fn poll(&mut self) -> Poll<Result<(), E>> {
        match self.pending.take() {
            Some(Err(error)) => Poll::Ready(Err(error)),
            _ => Poll::Ready(Ok(())),
        }
    }
}

impl<Si, Item, E> Sink<Item> for With<Si, Item, E>
where
    Si: Sink<Item>,
    E: From<Si::Error>,
{
    type Error = E;

    fn poll_ready(&mut self) -> Poll<Result<(), Self::Error>> {
        ready!(self.poll())?;
        ready!(self.sink.poll_ready()?);
        Poll::Ready(Ok(()))
    }
}

struct Failing;

impl Sink<u8> for Failing {
    type Error = u16;
    fn poll_ready(&mut self) -> Poll<Result<(), u16>> {
        Poll::Ready(Err(7))
    }
}

fn main() {
    let mut with: With<Failing, u8, u32> = With { sink: Failing, pending: None, _marker: PhantomData };
    assert_eq!(with.poll_ready(), Poll::Ready(Err(7u32)));
    with.pending = Some(Err(3));
    assert_eq!(with.poll_ready(), Poll::Ready(Err(3u32)));
}
