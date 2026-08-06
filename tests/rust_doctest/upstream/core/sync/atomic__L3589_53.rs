// Extracted from library/core/src/sync/atomic.rs:3589
#![allow(unused)]
fn main() {
    assert_eq!(foo.fetch_min(42, Ordering::Relaxed), 23);
    assert_eq!(foo.load(Ordering::Relaxed), 23);
    assert_eq!(foo.fetch_min(22, Ordering::Relaxed), 23);
    assert_eq!(foo.load(Ordering::Relaxed), 22);
}
