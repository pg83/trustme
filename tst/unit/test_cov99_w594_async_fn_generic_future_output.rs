//@ crate-type: lib
//@ compile-flags: --emit=metadata -Cpanic=abort

use std::future::Future;
use std::ops::AsyncFnOnce;

fn accepts_async<F, O>(_: F)
where
    F: AsyncFnOnce() -> O,
{
}

pub fn check<Fut: Future>(f: fn() -> Fut) {
    accepts_async::<_, Fut::Output>(f);
}
