// Extracted from library/core/src/pin.rs:1812
#![allow(unused)]
fn main() {
    use std::{
        future::Future,
        pin::pin,
        task::{Context, Poll},
        thread,
    };
    use std::{sync::Arc, task::Wake, thread::Thread};

    /// A waker that wakes up the current thread when called.
    struct ThreadWaker(Thread);

    impl Wake for ThreadWaker {
        fn wake(self: Arc<Self>) {
            self.0.unpark();
        }
    }

    /// Runs a future to completion.
    fn block_on<Fut: Future>(fut: Fut) -> Fut::Output {
        let waker_that_unparks_thread = // …
            Arc::new(ThreadWaker(thread::current())).into();
        let mut cx = Context::from_waker(&waker_that_unparks_thread);
        // Pin the future so it can be polled.
        let mut pinned_fut = pin!(fut);
        loop {
            match pinned_fut.as_mut().poll(&mut cx) {
                Poll::Pending => thread::park(),
                Poll::Ready(res) => return res,
            }
        }
    }

    assert_eq!(42, block_on(async { 42 }));
}
