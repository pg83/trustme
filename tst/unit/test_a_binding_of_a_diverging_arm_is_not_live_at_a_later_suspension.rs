//@ edition: 2021
// tokio's tcp_accept `no_extra_poll` spawns an async block that starts with
// `assert_ok!(tx.send(..))`, a `match` whose `Err(e)` arm panics, and then
// suspends in a loop. A binding's storage ends with its scope (rustc emits
// StorageDead there, diverging or not), so `e` is not live at the later
// suspension. Our MIR builder only reset a binding's state when it emitted
// the scope's drops; after a diverging arm `e` stayed "valid", was saved
// into the coroutine state, and dropping the suspended future dropped the
// never-written `e` (a null Arc).
use std::future::Future;
use std::pin::Pin;
use std::sync::Arc;
use std::task::{Context, Poll, Waker};

struct Pending;
impl Future for Pending {
    type Output = ();
    fn poll(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<()> {
        Poll::Pending
    }
}

fn send(v: Arc<u32>) -> Result<(), Arc<u32>> {
    drop(v);
    Ok(())
}

fn main() {
    let shared = Arc::new(5u32);
    let captured = shared.clone();
    let fut = async move {
        match send(Arc::clone(&captured)) {
            Ok(v) => v,
            Err(e) => panic!("{:?}", e),
        };
        loop {
            Pending.await;
        }
    };
    let mut fut = Box::pin(fut);
    let mut cx = Context::from_waker(Waker::noop());
    assert!(fut.as_mut().poll(&mut cx).is_pending());
    drop(fut);
    assert_eq!(Arc::strong_count(&shared), 1);
}
