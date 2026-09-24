//@ edition: 2021
// tokio's io_copy_bidirectional test crashed the compiler: a coroutine keeps
// its drop flags across a suspension in its state (SaveDropFlag). When the
// only remaining use of a flag was that save, the MIR garbage collector did
// not count it, dropped the flag, and left the save naming flag ~0u.
use std::cell::RefCell;
use std::future::Future;
use std::pin::{pin, Pin};
use std::task::{Context, Poll, Waker};

struct Log<'a>(u32, &'a RefCell<Vec<u32>>);
impl Drop for Log<'_> {
    fn drop(&mut self) {
        self.1.borrow_mut().push(self.0);
    }
}

struct YieldOnce(bool);
impl Future for YieldOnce {
    type Output = ();
    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<()> {
        if self.0 {
            Poll::Ready(())
        } else {
            self.0 = true;
            cx.waker().wake_by_ref();
            Poll::Pending
        }
    }
}

async fn retry(log: &RefCell<Vec<u32>>) -> u32 {
    let mut lock = None;
    let mut tries = 0;
    loop {
        if lock.is_none() {
            lock = Some(Log(1, log));
        }
        YieldOnce(false).await;
        tries += 1;
        if tries <= 1 {
            continue;
        }
        return tries;
    }
}

fn main() {
    let log = RefCell::new(Vec::new());
    let mut f = pin!(retry(&log));
    let mut cx = Context::from_waker(Waker::noop());
    let v = loop {
        if let Poll::Ready(v) = f.as_mut().poll(&mut cx) {
            break v;
        }
    };
    assert_eq!(v, 2);
    assert_eq!(*log.borrow(), [1]);
}
