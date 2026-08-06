// Extracted from library/core/src/sync/atomic.rs:2873
#![allow(unused)]
fn main() {
    assert_eq!(some_var.load(Ordering::Relaxed), 5);
}
