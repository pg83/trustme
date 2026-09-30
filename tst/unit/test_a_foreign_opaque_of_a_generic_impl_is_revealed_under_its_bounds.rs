//@ aux-build: opaque_generic_impl_core.rs
// axum awaits `Bytes::from_request(req, state)` inside its own generic
// extractor; `Bytes`'s impl lives in axum-core, generic over the state `S`
// with `S: Send + Sync`, and its `async fn from_request` is an opaque type of
// that impl method. After type checking the foreign signature takes the
// revealed form its defining crate recorded; working it out here instead
// meant finding the method through the impl, whose where-clauses hold only
// with the impl's own generics in scope, and without them code generation
// met the projection unresolved.
extern crate opaque_generic_impl_core;

use opaque_generic_impl_core::{Bytes, FromRequest, Request};
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};

pub struct Form(pub usize);

impl<S> FromRequest<S> for Form
where
    S: Send + Sync,
{
    type Rejection = String;
    async fn from_request(req: Request, state: &S) -> Result<Self, String> {
        let bytes = Bytes::from_request(req, state).await?;
        Ok(Form(bytes.0.len()))
    }
}

fn block_on<F: Future>(f: F) -> F::Output {
    let mut cx = Context::from_waker(Waker::noop());
    let mut f = pin!(f);
    loop {
        if let Poll::Ready(v) = f.as_mut().poll(&mut cx) {
            return v;
        }
    }
}

fn main() {
    let form = block_on(Form::from_request(Request(vec![1, 2, 3]), &())).map(|f| f.0);
    assert_eq!(form, Ok(3));
    let empty = block_on(<Form as FromRequest<()>>::from_request(Request(vec![]), &())).map(|f| f.0);
    assert_eq!(empty, Err("empty".to_string()));
}
