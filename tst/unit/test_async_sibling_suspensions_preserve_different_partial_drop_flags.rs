//@ edition: 2021
use std::cell::Cell;
use std::future::{pending, Future};
use std::rc::Rc;
use std::task::{Context, Waker};

struct Ticket(u32, Rc<Cell<u32>>);
impl Drop for Ticket {
    fn drop(&mut self) {
        self.1.set(self.1.get() + self.0);
    }
}

async fn pause(left: bool, count: Rc<Cell<u32>>) {
    let first = Ticket(1, count.clone());
    let second = Ticket(10, count);
    if left {
        drop(first);
        pending::<()>().await;
    } else {
        drop(second);
        pending::<()>().await;
    }
}

fn main() {
    for left in [true, false] {
        let count = Rc::new(Cell::new(0));
        let mut future = Box::pin(pause(left, count.clone()));
        let mut context = Context::from_waker(Waker::noop());
        assert!(future.as_mut().poll(&mut context).is_pending());
        assert_eq!(count.get(), if left { 1 } else { 10 });
        drop(future);
        assert_eq!(count.get(), 11);
    }
}
