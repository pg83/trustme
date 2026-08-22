use std::future::Future;
use std::mem::size_of_val;
use std::sync::atomic::{AtomicUsize, Ordering};

static DROPS: AtomicUsize = AtomicUsize::new(0);

struct Guard;

impl Drop for Guard {
    fn drop(&mut self) {
        DROPS.fetch_add(1, Ordering::Relaxed);
    }
}

struct Pair(Guard, usize);

async fn leaf(_value: [u8; 16]) {}

async fn forward(future: impl Future<Output = ()>) {
    future.await
}

async fn ignore(_: Guard) {}

async fn destructure(Pair(_, _): Pair) {}

fn main() {
    let future = forward(forward(forward(forward(forward(leaf([0; 16]))))));
    assert!(size_of_val(&future) > 550);

    let ignored = ignore(Guard);
    assert_eq!(DROPS.load(Ordering::Relaxed), 0);
    drop(ignored);
    assert_eq!(DROPS.load(Ordering::Relaxed), 1);

    let destructured = destructure(Pair(Guard, 0));
    assert_eq!(DROPS.load(Ordering::Relaxed), 1);
    drop(destructured);
    assert_eq!(DROPS.load(Ordering::Relaxed), 2);
}
