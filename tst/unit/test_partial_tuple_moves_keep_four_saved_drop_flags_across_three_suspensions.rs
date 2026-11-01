//@ compile-variant: -O
use std::cell::Cell;
use std::future::{poll_fn, Future};
use std::task::{Context, Poll, Waker};
struct Token<'a>(&'a Cell<u16>, u16);
impl Drop for Token<'_> { fn drop(&mut self) { self.0.set(self.0.get() + self.1); } }
async fn pause() {
    let mut waiting = true;
    poll_fn(|_| { if waiting { waiting = false; Poll::Pending } else { Poll::Ready(()) } }).await;
}
async fn work(events: &Cell<u16>, left: bool, right: bool) -> u16 {
    let first = (Token(events, 3), Token(events, 5));
    let selected_first = if left { first.0 } else { first.1 };
    pause().await;
    let second = (Token(events, 7), Token(events, 11));
    let selected_second = if right { second.0 } else { second.1 };
    let answer = selected_first.1 + selected_second.1;
    pause().await;
    drop(selected_first);
    pause().await;
    drop(selected_second);
    answer
}
fn main() {
    let mut context = Context::from_waker(Waker::noop());
    for left in [false, true] {
        for right in [false, true] {
            for stop in 1..=4 {
                let events = Cell::new(0);
                let mut future = Box::pin(work(&events, left, right));
                for step in 1..=stop {
                    let result = future.as_mut().poll(&mut context);
                    if step < 4 { assert!(result.is_pending()); }
                    else { assert_eq!(result, Poll::Ready((if left { 3 } else { 5 }) + (if right { 7 } else { 11 }))); }
                    if step < 3 { assert_eq!(events.get(), 0); }
                    if step == 3 { assert_eq!(events.get(), if left { 3 } else { 5 }); }
                }
                drop(future);
                assert_eq!(events.get(), if stop == 1 { 8 } else { 26 });
            }
        }
    }
}
