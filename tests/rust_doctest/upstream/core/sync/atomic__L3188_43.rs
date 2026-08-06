// Extracted from library/core/src/sync/atomic.rs:3188
#![allow(unused)]
fn main() {
    assert_eq!(foo.fetch_sub(10, Ordering::SeqCst), 20);
    assert_eq!(foo.load(Ordering::SeqCst), 10);
}
