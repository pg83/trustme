//@ edition: 2021
// tor-circmgr (arti): `get_or_launch` matches `error` with one arm moving a
// variant's field and another moving the whole value, then awaits. Dropping
// the future while it is suspended there runs a `Switch` over the enum that is
// guarded by the enum's drop flag. The future's drop function numbers its drop
// flags by the bits it saved and renumbers the body's flags it uses, but left
// a `Switch`'s flag with the body's number: past the end of its own flag
// table, so the remaining field went undropped (and the compiler wrote past
// its table while optimising, which crashed tor-circmgr).
use std::cell::Cell;
use std::future::Future;
use std::pin::pin;
use std::task::{Context, Poll, Waker};

thread_local! {
    static DROPS: Cell<u32> = const { Cell::new(0) };
}

struct Noisy(u32);

impl Drop for Noisy {
    fn drop(&mut self) {
        DROPS.with(|d| d.set(d.get() + self.0));
    }
}

#[allow(dead_code)]
enum Msg {
    Text(Noisy),
    Pair(Noisy, Noisy),
}

struct YieldOnce(bool);

impl Future for YieldOnce {
    type Output = ();
    fn poll(mut self: std::pin::Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<()> {
        if self.0 {
            Poll::Ready(())
        } else {
            self.0 = true;
            Poll::Pending
        }
    }
}

async fn body(m: Msg) -> u32 {
    match m {
        Msg::Pair(a, _) => drop(a),
        other => drop(other),
    }
    YieldOnce(false).await;
    7
}

fn main() {
    {
        let mut future = pin!(body(Msg::Pair(Noisy(1), Noisy(10))));
        let mut cx = Context::from_waker(Waker::noop());
        assert!(future.as_mut().poll(&mut cx).is_pending());
        assert_eq!(DROPS.with(|d| d.get()), 1);
    }
    assert_eq!(DROPS.with(|d| d.get()), 11);
}
