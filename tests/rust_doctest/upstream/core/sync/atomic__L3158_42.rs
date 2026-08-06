// Extracted from library/core/src/sync/atomic.rs:3158
#![allow(unused)]
fn main() {
    assert_eq!(foo.fetch_add(10, Ordering::SeqCst), 0);
    assert_eq!(foo.load(Ordering::SeqCst), 10);
}
