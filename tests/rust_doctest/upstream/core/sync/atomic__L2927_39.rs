// Extracted from library/core/src/sync/atomic.rs:2927
#![allow(unused)]
fn main() {
    assert_eq!(some_var.swap(10, Ordering::Relaxed), 5);
}
