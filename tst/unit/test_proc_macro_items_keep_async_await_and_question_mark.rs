//@ proc-macro-aux-build: numeric_token_bridge.rs
use numeric_token_bridge::intact;
#[intact]
mod work {
    pub async fn calculate(input: Result<u32, &'static str>) -> Result<u32, &'static str> {
        let value = input?;
        Ok(async move { value * 2 }.await + 1)
    }
}
fn main() {
    use std::future::Future;
    use std::task::{Context, Poll, Waker};
    let mut context = Context::from_waker(Waker::noop());
    let mut success = Box::pin(work::calculate(Ok(7)));
    assert_eq!(success.as_mut().poll(&mut context), Poll::Ready(Ok(15)));
    let mut failure = Box::pin(work::calculate(Err("stop")));
    assert_eq!(failure.as_mut().poll(&mut context), Poll::Ready(Err("stop")));
}
