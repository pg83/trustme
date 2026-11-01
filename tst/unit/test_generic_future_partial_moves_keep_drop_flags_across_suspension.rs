//@ compile-variant: -O
use std::cell::Cell;
use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll, Waker};

struct Pause(bool);
impl Future for Pause {
    type Output = ();
    fn poll(mut self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<()> {
        if self.0 { Poll::Ready(()) } else { self.0 = true; Poll::Pending }
    }
}
struct Token<'a>(&'a Cell<u16>, u16);
impl Drop for Token<'_> { fn drop(&mut self) { self.0.set(self.0.get() + self.1); } }
async fn work<F: Future<Output = ()>>(step: F, first: bool, events: &Cell<u16>) -> u16 {
    let pair = (Token(events, 3), Token(events, 5));
    let selected = if first { pair.0 } else { pair.1 };
    step.await;
    drop(selected);
    7
}
fn main() {
    let mut context = Context::from_waker(Waker::noop());
    let cancelled = Cell::new(0);
    let mut first = Box::pin(work(Pause(false), true, &cancelled));
    assert!(first.as_mut().poll(&mut context).is_pending());
    assert_eq!(cancelled.get(), 0);
    drop(first);
    assert_eq!(cancelled.get(), 8);
    let completed = Cell::new(0);
    let mut second = Box::pin(work(Pause(false), false, &completed));
    assert!(second.as_mut().poll(&mut context).is_pending());
    assert_eq!(second.as_mut().poll(&mut context), Poll::Ready(7));
    assert_eq!(completed.get(), 8);
    drop(second);
    assert_eq!(completed.get(), 8);
}
