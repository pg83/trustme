use std::cell::Cell;
use std::future::{poll_fn, AsyncDrop};
use std::pin::Pin;
use std::task::Poll;
pub struct Deferred<'a>(pub &'a Cell<usize>, pub usize);
pub struct Immediate<'a>(pub &'a Cell<usize>, pub usize);
impl Drop for Deferred<'_> { fn drop(&mut self) { self.0.set(9999); } }
impl Drop for Immediate<'_> { fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); } }
impl AsyncDrop for Deferred<'_> {
    async fn drop(self: Pin<&mut Self>) {
        let mut remaining = 1;
        poll_fn(|_| {
            if remaining == 0 { Poll::Ready(()) } else { remaining -= 1; Poll::Pending }
        }).await;
        self.0.set(self.0.get() * 10 + self.1);
    }
}
