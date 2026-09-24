//@ edition: 2021
// tokio's mpsc `reserve_inner` declares a guard, then awaits a semaphore
// `Acquire`; dropping the suspended future must drop the `Acquire` (which
// returns its permits) before the guard (which wakes the receiver once the
// channel is idle). A suspended coroutine is dropped along the drop path
// of its suspension point, innermost scope first: the awaited temporary,
// then the variables in reverse declaration order. We dropped the saved
// locals in declaration order.
use std::cell::RefCell;
use std::future::Future;
use std::pin::Pin;
use std::rc::Rc;
use std::task::{Context, Poll, Waker};

type LogV = Rc<RefCell<Vec<&'static str>>>;
struct Log(&'static str, LogV);
impl Drop for Log {
    fn drop(&mut self) {
        self.1.borrow_mut().push(self.0);
    }
}
struct Pend(Log);
impl Future for Pend {
    type Output = ();
    fn poll(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<()> {
        Poll::Pending
    }
}
async fn body(log: LogV) {
    let _first = Log("first", log.clone());
    let _guard = Log("guard", log.clone());
    Pend(Log("awaitee", log.clone())).await;
}
fn main() {
    let log: LogV = Rc::new(RefCell::new(Vec::new()));
    let mut f = Box::pin(body(log.clone()));
    let mut cx = Context::from_waker(Waker::noop());
    assert!(f.as_mut().poll(&mut cx).is_pending());
    drop(f);
    assert_eq!(*log.borrow(), ["awaitee", "guard", "first"]);
}
