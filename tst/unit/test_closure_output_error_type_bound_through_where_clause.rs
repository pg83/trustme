// `rx.map(|l| stream::iter(l.into_iter().map(|i| Ok(i))))` leaves the error
// type of the inner `Result` open: `stream::iter<J, T, E>` learns `E` from
// nothing at the call.  `flatten_stream` requires `<Self as Future>::Item:
// Stream<Error = Self::Error>`, so the closure's output `IterStream<Map<..>>`
// must be a `Stream` whose `Error` is `u32`, and its impl relates
// `Result<T, E>` with the inner closure's output `Result<usize, ?e>` once `E`
// is `u32`.  That binds a variable of the goal, not a parameter of the impl;
// upstream keeps the binding in the inference context and returns it as the
// variable's value.  Here the probe that related them was undone and the
// binding with it: `?e` was never inferred.

fn main() {
    if false { test(); }
}

fn test() {
    let rx = Err::<Vec<usize>, u32>(1).into_future();
    rx.map(|l: Vec<usize>| stream::iter(l.into_iter().map(|i| Ok(i))))
      .flatten_stream();
}

use future::{Future, IntoFuture};
mod future {
    use std::result;
    use crate::stream;

    pub trait Future {
        type Item;
        type Error;

        fn map<F, U>(self, _: F) -> Map<Self, F>
            where F: FnOnce(Self::Item) -> U,
                  Self: Sized,
        {
            panic!()
        }

        fn flatten_stream(self) -> FlattenStream<Self>
            where <Self as Future>::Item: stream::Stream<Error=Self::Error>,
                  Self: Sized
        {
            panic!()
        }
    }

    pub trait IntoFuture {
        type Future: Future<Item=Self::Item, Error=Self::Error>;
        type Item;
        type Error;
        fn into_future(self) -> Self::Future;
    }

    impl<F: Future> IntoFuture for F {
        type Future = F;
        type Item = F::Item;
        type Error = F::Error;

        fn into_future(self) -> F {
            panic!()
        }
    }

    impl<T, E> IntoFuture for result::Result<T, E> {
        type Future = FutureResult<T, E>;
        type Item = T;
        type Error = E;

        fn into_future(self) -> FutureResult<T, E> {
            panic!()
        }
    }

    pub struct Map<A, F> {
        _a: (A, F),
    }

    impl<U, A, F> Future for Map<A, F>
        where A: Future,
              F: FnOnce(A::Item) -> U,
    {
        type Item = U;
        type Error = A::Error;
    }

    pub struct FlattenStream<F> {
        _f: F,
    }

    pub struct FutureResult<T, E> {
        _inner: (T, E),
    }

    impl<T, E> Future for FutureResult<T, E> {
        type Item = T;
        type Error = E;
    }
}

mod stream {
    pub trait Stream {
        type Item;
        type Error;
    }

    pub struct IterStream<I> {
        _iter: I,
    }

    pub fn iter<J, T, E>(_: J) -> IterStream<J::IntoIter>
        where J: IntoIterator<Item=Result<T, E>>,
    {
        panic!()
    }

    impl<I, T, E> Stream for IterStream<I>
        where I: Iterator<Item=Result<T, E>>,
    {
        type Item = T;
        type Error = E;
    }
}
