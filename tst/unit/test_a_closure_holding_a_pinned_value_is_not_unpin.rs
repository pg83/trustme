// tokio's async_send_sync builds `poll_fn(move |_| { let _ = &pinned; .. })`
// around a `PhantomPinned` and probes `!Unpin` with an impl selection that
// is ambiguous if the value is `Unpin`. `PollFn<F>` is `Unpin` when `F`
// is, and the closure holds the `PhantomPinned` it moved in, so the probe
// picks the one impl that always applies.
use std::future::poll_fn;
use std::marker::PhantomPinned;
use std::task::Poll;

trait AmbiguousIfUnpin<A> {
    fn some_item(&self) -> u8 {
        7
    }
}

impl<T: ?Sized> AmbiguousIfUnpin<()> for T {}
impl<T: ?Sized + Unpin> AmbiguousIfUnpin<[u8; 0]> for T {}

fn main() {
    let pinned = PhantomPinned;
    let f = poll_fn(move |_| {
        let _ = &pinned;
        Poll::Pending::<()>
    });
    assert_eq!(AmbiguousIfUnpin::some_item(&f), 7);
}
