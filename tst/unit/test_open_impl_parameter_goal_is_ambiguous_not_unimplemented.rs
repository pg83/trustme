/* `Waker: From<?1>` is checked while the argument is still coercing into `?1`.  Its
   candidates are the reflexive `impl<T> From<T> for T`, the reservation impl for `!`,
   and `impl<W: Wake + Send + Sync> From<Arc<W>> for Waker`, whose head leaves `W` open.
   That candidate's nested goal `?W: Wake` is ambiguous, not unimplemented (rustc
   `assemble_candidates`: a goal on an inference variable is ambiguous at any depth),
   so no candidate wins and the argument decides `?1 = Arc<Noop>`.  Selecting the
   reflexive impl instead made `Arc<Noop>` mismatch `Waker`. */
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
