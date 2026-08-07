// Extracted from library/core/src/sync/atomic.rs:606
#![allow(unused)]
#![feature(atomic_from_mut)]
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};

    let mut some_bool = true;
    let a = AtomicBool::from_mut(&mut some_bool);
    a.store(false, Ordering::Relaxed);
    assert_eq!(some_bool, false);
}
