//@ aux-build: opaque_return_core.rs
// axum's `Form` extractor matches `req.extract().await` against
// `Ok(RawForm(bytes))`, and `extract` comes from another crate returning
// `impl Future<Output = Result<E, E::Rejection>>`. Upstream keeps the opaque
// type opaque outside its defining crate: `.await` reads `Output` off its
// bounds, and the pattern fixes `E`. The signature reached this crate with
// the hidden type substituted - `<E as FromRequest>::from_request`'s own
// opaque, a projection nothing normalizes while `E` is open - and the match
// could not infer `E`.
extern crate opaque_return_core;

use opaque_return_core::{extract, FromRequest, Request};
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};

pub struct RawForm(pub Vec<u8>);

impl FromRequest for RawForm {
    type Rejection = String;
    async fn from_request(req: Request) -> Result<Self, String> {
        if req.0.is_empty() { Err("empty".to_string()) } else { Ok(RawForm(req.0)) }
    }
}

async fn form(req: Request) -> Result<usize, String> {
    match extract(req).await {
        Ok(RawForm(bytes)) => Ok(bytes.len()),
        Err(e) => Err(e),
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
    assert_eq!(block_on(form(Request(vec![1, 2, 3]))), Ok(3));
    assert_eq!(block_on(form(Request(vec![]))), Err("empty".to_string()));
}
