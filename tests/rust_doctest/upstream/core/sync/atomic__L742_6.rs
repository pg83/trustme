// Extracted from library/core/src/sync/atomic.rs:742
#![allow(unused)]
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};

    let some_bool = AtomicBool::new(true);

    some_bool.store(false, Ordering::Relaxed);
    assert_eq!(some_bool.load(Ordering::Relaxed), false);
}
