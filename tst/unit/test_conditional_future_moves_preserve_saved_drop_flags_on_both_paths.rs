//@ compile-variant: -O
use std::cell::Cell;
use std::future::{poll_fn, Future};
use std::task::{Context, Poll, Waker};
struct Token<'a>(&'a Cell<u32>, u32);
impl Drop for Token<'_> { fn drop(&mut self) { self.0.set(self.0.get() + self.1); } }
async fn pause() {
    let mut pending = true;
    poll_fn(|_| { if pending { pending = false; Poll::Pending } else { Poll::Ready(()) } }).await;
}
async fn work(events: &Cell<u32>, first: bool, second: bool) {
    let tokens = (Token(events, 3), Token(events, 7));
    if first { drop(tokens.0); } else { drop(tokens.1); }
    pause().await;
    if second { drop(Token(events, 11)); } else { drop(Token(events, 13)); }
    pause().await;
}
fn main() {
    let mut context = Context::from_waker(Waker::noop());
    for first in [false, true] {
        for second in [false, true] {
            for stop in 1..=3 {
                let events = Cell::new(0);
                let mut future = Box::pin(work(&events, first, second));
                for step in 1..=stop {
                    let state = future.as_mut().poll(&mut context);
                    assert_eq!(state.is_ready(), step == 3);
                    let extra = if step < 2 { 0 } else if second { 11 } else { 13 };
                    assert_eq!(events.get(), if step == 3 { 10 + extra } else { (if first { 3 } else { 7 }) + extra });
                }
                drop(future);
                assert_eq!(events.get(), 10 + if stop == 1 { 0 } else if second { 11 } else { 13 });
            }
        }
    }
}
