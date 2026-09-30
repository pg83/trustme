// tokio-util's `impl<T, I, U> Sink<I> for FramedImpl<T, U, W> where U:
// Encoder<I>, U::Error: From<io::Error>` has `ready!(self.poll_flush())?;`
// in `poll_close`. The `?` calls `FromResidual::from_residual(residual)`,
// whose `Self` is a variable of that call: rustc relates it to the function's
// return type only after the call, so while the residual is coerced into `R`
// the goal `?S: FromResidual<R>` is ambiguous and `R` comes from the
// residual. We had the return type in `Self` already and selected the goal
// while the residual's type was still unknown; its nested `U::Error:
// From<?E>` took the where-clause and read `?E` as `io::Error`.
use std::io;
use std::task::{ready, Poll};

pub trait Encoder<Item> {
    type Error: From<io::Error>;
    fn encode(&mut self, item: Item) -> Result<(), Self::Error>;
}

pub trait Sink<Item> {
    type Error;
    fn poll_flush(&mut self) -> Poll<Result<(), Self::Error>>;
    fn poll_close(&mut self) -> Poll<Result<(), Self::Error>>;
}

pub struct FramedImpl<T, U> {
    inner: T,
    codec: U,
}

fn shutdown<T>(_: &mut T) -> Poll<io::Result<()>> {
    Poll::Ready(Ok(()))
}

impl<T, I, U> Sink<I> for FramedImpl<T, U>
where
    U: Encoder<I>,
    U::Error: From<io::Error>,
{
    type Error = U::Error;

    fn poll_flush(&mut self) -> Poll<Result<(), Self::Error>> {
        Poll::Ready(Ok(()))
    }

    fn poll_close(&mut self) -> Poll<Result<(), Self::Error>> {
        ready!(self.poll_flush())?;
        Poll::Ready(Ok(()))
    }
}

struct Lines;

impl Encoder<String> for Lines {
    type Error = io::Error;
    fn encode(&mut self, _: String) -> Result<(), io::Error> {
        Ok(())
    }
}

fn main() {
    let mut framed = FramedImpl { inner: (), codec: Lines };
    let _ = framed.codec.encode(String::new());
    assert!(matches!(<FramedImpl<(), Lines> as Sink<String>>::poll_close(&mut framed), Poll::Ready(Ok(()))));
}
