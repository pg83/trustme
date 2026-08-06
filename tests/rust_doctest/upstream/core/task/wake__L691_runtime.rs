// Extracted from library/core/src/task/wake.rs:691
#![allow(unused)]
#![feature(local_waker)]
fn main() {
    use std::future::{Future, poll_fn};
    use std::task::Poll;
    
    // a future that returns pending once.
    fn yield_now() -> impl Future<Output=()> + Unpin {
        let mut yielded = false;
        poll_fn(move |cx| {
            if !yielded {
                yielded = true;
                cx.local_waker().wake_by_ref();
                return Poll::Pending;
            }
            return Poll::Ready(())
        })
    }
    
    async fn __() {
    yield_now().await;
    }
}
