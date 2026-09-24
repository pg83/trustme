//@ edition: 2021
// tokio's `select!` builds `let mut futures = &mut futures;` and a closure
// using `&mut *futures`, awaited through `poll_fn`, and tests the future's
// size. A borrow of a place through a dereference borrows what the
// reference points at, not the reference's local (upstream
// `MaybeBorrowedLocals`), so the local is not kept across the `.await`.
use std::future::{poll_fn, Future};
use std::mem::size_of_val;
use std::task::Poll;

async fn nop() {}

fn size<F: Future>(f: &F) -> usize {
    size_of_val(f)
}

fn main() {
    let reborrowed = async {
        let mut a = [0u8; 8];
        let t = &mut a;
        let r = &mut *t;
        nop().await;
        r[0]
    };
    assert_eq!(size(&reborrowed), 24);
    let polled = async {
        let mut a = [0u8; 8];
        let t = &mut a;
        poll_fn(|_| {
            t[0] += 1;
            Poll::Ready(())
        })
        .await
    };
    assert_eq!(size(&polled), 24);
}
