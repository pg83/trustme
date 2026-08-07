// Extracted from library/core/src/sync/atomic.rs:1180
#![allow(unused)]
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};

    let foo = AtomicBool::new(true);
    assert_eq!(foo.fetch_xor(false, Ordering::SeqCst), true);
    assert_eq!(foo.load(Ordering::SeqCst), true);

    let foo = AtomicBool::new(true);
    assert_eq!(foo.fetch_xor(true, Ordering::SeqCst), true);
    assert_eq!(foo.load(Ordering::SeqCst), false);

    let foo = AtomicBool::new(false);
    assert_eq!(foo.fetch_xor(false, Ordering::SeqCst), false);
    assert_eq!(foo.load(Ordering::SeqCst), false);
}
