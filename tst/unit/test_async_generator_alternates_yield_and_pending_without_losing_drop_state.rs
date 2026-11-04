//@ edition: 2024
//@ compile-variant: -O
#![feature(gen_blocks, async_iterator)]

use std::async_iter::AsyncIterator;
use std::cell::Cell;
use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll, Waker};

struct Token<'a>(&'a Cell<u32>, u32);
impl Drop for Token<'_> {
    fn drop(&mut self) { self.0.set(self.0.get() * 10 + self.1); }
}

struct OncePending(bool);
impl Future for OncePending {
    type Output = u32;
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<u32> {
        if self.0 { Poll::Ready(5) } else {
            self.0 = true;
            cx.waker().wake_by_ref();
            Poll::Pending
        }
    }
}

fn main() {
    let mut cx = Context::from_waker(Waker::noop());
    for polls in 1..=5 {
        let events = Cell::new(0);
        let log = &events;
        let mut task = Box::pin(async gen move {
            let outer = Token(log, 1);
            yield 3;
            let inner = Token(log, 2);
            let value = OncePending(false).await;
            yield value;
            drop(inner);
            yield value + 3;
            drop(outer);
        });
        assert_eq!(task.as_mut().poll_next(&mut cx), Poll::Ready(Some(3)));
        assert_eq!(events.get(), 0);
        if polls >= 2 {
            assert!(task.as_mut().poll_next(&mut cx).is_pending());
            assert_eq!(events.get(), 0);
        }
        if polls >= 3 {
            assert_eq!(task.as_mut().poll_next(&mut cx), Poll::Ready(Some(5)));
            assert_eq!(events.get(), 0);
        }
        if polls >= 4 {
            assert_eq!(task.as_mut().poll_next(&mut cx), Poll::Ready(Some(8)));
            assert_eq!(events.get(), 2);
        }
        if polls == 5 {
            assert_eq!(task.as_mut().poll_next(&mut cx), Poll::Ready(None));
            assert_eq!(task.as_mut().poll_next(&mut cx), Poll::Ready(None));
            assert_eq!(events.get(), 21);
        }
        drop(task);
        assert_eq!(events.get(), if polls == 1 { 1 } else { 21 });
    }
}
