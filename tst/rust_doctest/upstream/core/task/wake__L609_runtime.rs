// Extracted from library/core/src/task/wake.rs:609
#![allow(unused)]
fn main() {
    use std::future::Future;
    use std::pin::Pin;
    use std::sync::{Arc, Mutex};
    use std::task::{Context, Poll, Waker};

    struct Waiter {
        shared: Arc<Mutex<Shared>>,
    }

    struct Shared {
        waker: Waker,
        // ...
    }

    impl Future for Waiter {
        type Output = ();
        fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<()> {
            let mut shared = self.shared.lock().unwrap();

            // update the waker
            shared.waker.clone_from(cx.waker());

            // readiness logic ...
          Poll::Ready(())
        }
    }
}
