use std::cell::Cell;
use std::future::Future;
use std::pin::Pin;
use std::task::{Context, Poll, Waker};

struct Token<'a>(usize, &'a Cell<u64>);
impl Drop for Token<'_> {
    fn drop(&mut self) {
        let bit = 1u64 << self.0;
        assert_eq!(self.1.get() & bit, 0);
        self.1.set(self.1.get() | bit);
    }
}
struct Pause;
impl Future for Pause {
    type Output = ();
    fn poll(self: Pin<&mut Self>, _: &mut Context<'_>) -> Poll<()> { Poll::Pending }
}
async fn hold(log: &Cell<u64>, keep_owner: bool) {
    let values: Box<[Token<'_>; 32]> = Box::new(std::array::from_fn(|i| Token(i, log)));
    if keep_owner {
        let [first, ..] = *values;
        drop(first);
    } else {
        drop(values);
    }
    Pause.await;
}
fn main() {
    for keep_owner in [false, true] {
        let log = Cell::new(0);
        let mut future = Box::pin(hold(&log, std::hint::black_box(keep_owner)));
        let mut context = Context::from_waker(Waker::noop());
        assert!(future.as_mut().poll(&mut context).is_pending());
        assert_eq!(log.get(), if keep_owner { 1 } else { u32::MAX as u64 });
        drop(future);
        assert_eq!(log.get(), u32::MAX as u64);
    }
}
