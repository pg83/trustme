//@ run-pass
// Two promoted borrows of the same value are the same value, and library code
// compares their addresses -- `Waker::will_wake` says two wakers wake the same
// task when their vtables sit at one address. The two `&RawWakerVTable`s are
// promoted inside two generic functions, so the sharing has to reach a
// promoted value that a body carries per instantiation.

use std::sync::Arc;
use std::task::{Wake, Waker};

struct Noop;

impl Wake for Noop {
    fn wake(self: Arc<Self>) {}
}

fn main() {
    let waker = Waker::from(Arc::new(Noop));
    let cloned = waker.clone();
    assert!(waker.will_wake(&cloned));
}
