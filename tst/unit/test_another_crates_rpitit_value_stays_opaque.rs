//@ aux-build: opaque_rpitit_core.rs
// The same `.await` as axum's `Form` extractor through a trait method:
// `RequestExt::extract` returns `impl Future<Output = Result<E,
// E::Rejection>>` from a trait of another crate, and the impl's value for it
// is that impl method's opaque type. Upstream normalizes the projection to
// the opaque and reads `Output` off its bounds; here the impl's value arrived
// with its hidden type substituted, a projection that stays open with `E`.
extern crate opaque_rpitit_core;

use opaque_rpitit_core::{FromRequest, Request, RequestExt};
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};

pub struct RawForm(pub Vec<u8>);

impl<S: Sync> FromRequest<S> for RawForm {
    type Rejection = String;
    async fn from_request(req: Request, _state: &S) -> Result<Self, String> {
        if req.0.is_empty() { Err("empty".to_string()) } else { Ok(RawForm(req.0)) }
    }
}

async fn form(req: Request) -> Result<usize, String> {
    match req.extract().await {
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
