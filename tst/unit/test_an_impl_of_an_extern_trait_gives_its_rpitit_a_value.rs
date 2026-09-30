//@ aux-build: rpitit_from_parts.rs
// axum's `ConnectInfo` extractor awaits `Extension::<Self>::from_request_parts(
// parts, state)`, an `async fn` of axum-core's `FromRequestParts`. Its future
// is the trait's return-position `impl Future`, and the impl gives that
// synthetic associated type a value: the impl method's own opaque type. For a
// trait of this crate the value was recorded when the impl method was matched
// against the trait's; a trait read from another crate spells its return type
// with the projection instead, and that form recorded nothing. The projection
// had no value in the impl, the candidate stayed ambiguous with every bound
// proven, and the future's type was never known.
extern crate rpitit_from_parts;

use rpitit_from_parts::FromParts;
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};

#[derive(Debug, PartialEq)]
struct Extension(u32);

impl<S: Sync> FromParts<S> for Extension {
    async fn from_parts(parts: &mut u32, _state: &S) -> Option<Self> {
        *parts += 1;
        Some(Extension(*parts))
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

async fn twice(parts: &mut u32) -> Option<(Extension, Extension)> {
    let first = Extension::from_parts(parts, &()).await?;
    let second = <Extension as FromParts<u8>>::from_parts(parts, &7).await?;
    Some((first, second))
}

fn main() {
    let mut parts = 5;
    assert_eq!(block_on(Extension::from_parts(&mut parts, &())), Some(Extension(6)));
    assert_eq!(block_on(twice(&mut parts)), Some((Extension(7), Extension(8))));
}
