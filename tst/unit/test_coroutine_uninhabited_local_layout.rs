use std::future::Future;
use std::task::{Context, Poll, Waker};

enum Never {}

fn never() -> Never {
    panic!()
}

#[allow(unused_mut, unused_variables)]
async fn includes_never(crash: bool, value: u32) -> u32 {
    let mut result = async { value * value }.await;
    if !crash {
        return result;
    }
    let bad = never();
    result *= async { value + value }.await;
    drop(bad);
    result
}

fn main() {
    let mut future = Box::pin(includes_never(false, 4));
    let mut context = Context::from_waker(Waker::noop());
    assert!(matches!(future.as_mut().poll(&mut context), Poll::Ready(16)));
}
