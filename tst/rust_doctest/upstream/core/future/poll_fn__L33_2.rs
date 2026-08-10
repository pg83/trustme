// Extracted from library/core/src/future/poll_fn.rs:33
#![allow(unused)]
fn main() {
    async fn run() {
    use core::future::{self, Future};
    use core::task::Poll;

    /// Resolves to the first future that completes. In the event of a tie, `a` wins.
    fn naive_select<T>(
        a: impl Future<Output = T>,
        b: impl Future<Output = T>,
    ) -> impl Future<Output = T>
    {
        let (mut a, mut b) = (Box::pin(a), Box::pin(b));
        future::poll_fn(move |cx| {
            if let Poll::Ready(r) = a.as_mut().poll(cx) {
                Poll::Ready(r)
            } else if let Poll::Ready(r) = b.as_mut().poll(cx) {
                Poll::Ready(r)
            } else {
                Poll::Pending
            }
        })
    }

    let a = async { 42 };
    let b = future::pending();
    let v = naive_select(a, b).await;
    assert_eq!(v, 42);

    let a = future::pending();
    let b = async { 27 };
    let v = naive_select(a, b).await;
    assert_eq!(v, 27);

    let a = async { 42 };
    let b = async { 27 };
    let v = naive_select(a, b).await;
    assert_eq!(v, 42); // biased towards `a` in case of tie!
    }
}
