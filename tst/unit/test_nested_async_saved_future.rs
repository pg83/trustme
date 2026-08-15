use std::future::Future;
use std::pin::Pin;
use std::sync::Arc;
use std::task::{Context, Poll, Wake, Waker};

struct NoopWaker;

impl Wake for NoopWaker {
    fn wake(self: Arc<Self>) {}
}

fn block_on<F: Future>(future: F) -> F::Output {
    let waker = Waker::from(Arc::new(NoopWaker));
    let mut context = Context::from_waker(&waker);
    let mut future = Box::pin(future);
    loop {
        match Pin::as_mut(&mut future).poll(&mut context) {
            Poll::Ready(value) => return value,
            Poll::Pending => {}
        }
    }
}

async fn add(left: u32, right: u32) -> u32 {
    let inner = async move { left + right };
    inner.await
}

fn main() {
    assert_eq!(block_on(add(19, 23)), 42);
}
