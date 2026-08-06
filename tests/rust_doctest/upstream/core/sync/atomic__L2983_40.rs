// Extracted from library/core/src/sync/atomic.rs:2983
#![allow(unused)]
fn main() {
    assert_eq!(some_var.compare_and_swap(5, 10, Ordering::Relaxed), 5);
    assert_eq!(some_var.load(Ordering::Relaxed), 10);
    
    assert_eq!(some_var.compare_and_swap(6, 12, Ordering::Relaxed), 10);
    assert_eq!(some_var.load(Ordering::Relaxed), 10);
}
