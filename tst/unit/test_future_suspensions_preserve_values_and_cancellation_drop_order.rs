//@ compile-variant: -O
use std::cell::Cell;
use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll, Waker};

struct Token<'a>(&'a Cell<u32>, u32);
impl Drop for Token<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); }
}

struct OncePending(bool, u32);
impl Future for OncePending {
    type Output = u32;
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<u32> {
        if self.0 { Poll::Ready(self.1) } else {
            self.0 = true;
            cx.waker().wake_by_ref();
            Poll::Pending
        }
    }
}

fn main() {
    let mut cx = Context::from_waker(Waker::noop());
    for polls in 1..=3 {
        let events = Cell::new(0);
        let log = &events;
        let mut task = Box::pin(async move {
            let outer = Token(log, 1);
            let left = OncePending(false, 7).await;
            let inner = Token(log, 2);
            let right = OncePending(false, left + 3).await;
            drop(inner);
            drop(outer);
            left + right
        });
        assert!(task.as_mut().poll(&mut cx).is_pending());
        assert_eq!(events.get(), 0);
        if polls >= 2 {
            assert!(task.as_mut().poll(&mut cx).is_pending());
            assert_eq!(events.get(), 0);
        }
        if polls == 3 {
            assert_eq!(task.as_mut().poll(&mut cx), Poll::Ready(17));
            assert_eq!(events.get(), 21);
        }
        drop(task);
        assert_eq!(events.get(), if polls == 1 { 1 } else { 21 });
    }
}
