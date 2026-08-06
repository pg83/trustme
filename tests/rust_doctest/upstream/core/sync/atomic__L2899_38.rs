// Extracted from library/core/src/sync/atomic.rs:2899
#![allow(unused)]
fn main() {
    some_var.store(10, Ordering::Relaxed);
    assert_eq!(some_var.load(Ordering::Relaxed), 10);
}
