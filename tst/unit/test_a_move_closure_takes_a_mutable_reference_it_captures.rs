//@ edition: 2021
// tokio's `join!` moves `&mut futures` into the `poll_fn` closure it awaits,
// and tests the size of the resulting future. A capture is either a borrow
// of the place or a move of it (upstream `closure_analyze`); a `move`
// closure moves the reference in. We reborrowed a captured `&mut`, which
// kept the original reference alive across the `.await` and in the
// coroutine's saved locals.
use std::future::{poll_fn, Future};
use std::mem::size_of_val;
use std::task::Poll;

async fn nop() {}

fn size<F: Future>(f: &F) -> usize {
    size_of_val(f)
}

fn main() {
    let awaited = async {
        let mut t = [0u8; 8];
        let t = &mut t;
        poll_fn(move |_| {
            t[0] += 1;
            Poll::Ready(())
        })
        .await
    };
    assert_eq!(size(&awaited), 24);
    let stored = async {
        let mut t = [0u8; 8];
        let t = &mut t;
        let mut f = move || {
            t[0] += 1;
        };
        nop().await;
        f()
    };
    assert_eq!(size(&stored), 24);
}
