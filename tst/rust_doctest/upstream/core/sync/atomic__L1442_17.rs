// Extracted from library/core/src/sync/atomic.rs:1442
#![allow(unused)]
#![feature(atomic_try_update)]
fn main() {

    use std::sync::atomic::{AtomicBool, Ordering};

    let x = AtomicBool::new(false);
    assert_eq!(x.update(Ordering::SeqCst, Ordering::SeqCst, |x| !x), false);
    assert_eq!(x.update(Ordering::SeqCst, Ordering::SeqCst, |x| !x), true);
    assert_eq!(x.load(Ordering::SeqCst), false);
}
