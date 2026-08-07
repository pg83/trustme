// Extracted from library/core/src/sync/atomic.rs:773
#![allow(unused)]
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};

    let some_bool = AtomicBool::new(true);

    assert_eq!(some_bool.swap(false, Ordering::Relaxed), true);
    assert_eq!(some_bool.load(Ordering::Relaxed), false);
}
