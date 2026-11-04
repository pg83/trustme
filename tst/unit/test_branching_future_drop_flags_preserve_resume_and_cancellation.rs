//@ compile-variant: -O
use std::cell::Cell;
use std::rc::Rc;
use std::future::{poll_fn, Future};
use std::task::{Context, Poll, Waker};
struct Mark(u32, Rc<Cell<u32>>);
impl Drop for Mark { fn drop(&mut self) { self.1.set(self.1.get() + self.0); } }
async fn yield_once() {
    let mut ready = false;
    poll_fn(move |context| {
        if ready { Poll::Ready(()) } else { ready = true; context.waker().wake_by_ref(); Poll::Pending }
    }).await;
}
async fn work(flag: bool, count: Rc<Cell<u32>>) {
    let first = Mark(3, count.clone());
    let second = Mark(5, count);
    if flag { drop(first); yield_once().await; drop(second); }
    else { drop(second); yield_once().await; drop(first); }
}
fn main() {
    for flag in [false, true] {
        for finish in [false, true] {
            let count = Rc::new(Cell::new(0));
            let mut future = Box::pin(work(std::hint::black_box(flag), count.clone()));
            let mut context = Context::from_waker(Waker::noop());
            assert_eq!(future.as_mut().poll(&mut context), Poll::Pending);
            assert_eq!(count.get(), if flag { 3 } else { 5 });
            if finish { assert_eq!(future.as_mut().poll(&mut context), Poll::Ready(())); }
            drop(future);
            assert_eq!(count.get(), 8);
        }
    }
}
