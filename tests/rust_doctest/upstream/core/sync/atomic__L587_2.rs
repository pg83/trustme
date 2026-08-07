// Extracted from library/core/src/sync/atomic.rs:587
#![allow(unused)]
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};

    let mut some_bool = AtomicBool::new(true);
    assert_eq!(*some_bool.get_mut(), true);
    *some_bool.get_mut() = false;
    assert_eq!(some_bool.load(Ordering::SeqCst), false);
}
