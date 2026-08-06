// Extracted from library/core/src/sync/atomic.rs:3545
#![allow(unused)]
fn main() {
    assert_eq!(foo.fetch_max(42, Ordering::SeqCst), 23);
    assert_eq!(foo.load(Ordering::SeqCst), 42);
}
