// futures' tests write `block_on(async { let mut fut = pin!(fut); .. })`
// with `fut` an async block. Upstream's builtin `Copy`/`Clone` candidates
// cover a closure through its captures but never an async block or a
// coroutine (`instantiate_constituent_tys_for_copy_clone_trait`); we took the
// async block for `Copy`, captured it by reference and then moved out of
// the borrow in `pin!`.
use std::future::Future;
use std::pin::{pin, Pin};
use std::task::{Context, Poll, Waker};
fn block_on<F: Future>(fut: F) -> F::Output {
    let mut fut = pin!(fut);
    let mut cx = Context::from_waker(Waker::noop());
    loop {
        if let Poll::Ready(v) = fut.as_mut().poll(&mut cx) { return v; }
    }
}
fn poll_once<F: Future + Unpin>(f: &mut F) -> Poll<F::Output> {
    let mut cx = Context::from_waker(Waker::noop());
    Pin::new(f).poll(&mut cx)
}
fn main() {
    let fut = async { 5u8 };
    let r = block_on(async {
        let mut fut = pin!(fut);
        poll_once(&mut fut)
    });
    assert_eq!(r, Poll::Ready(5));
}
