// Extracted from library/core/src/sync/atomic.rs:3035
#![allow(unused)]
fn main() {
    assert_eq!(some_var.compare_exchange(5, 10,
                                         Ordering::Acquire,
                                         Ordering::Relaxed),
               Ok(5));
    assert_eq!(some_var.load(Ordering::Relaxed), 10);
    
    assert_eq!(some_var.compare_exchange(6, 12,
                                         Ordering::SeqCst,
                                         Ordering::Acquire),
               Err(10));
    assert_eq!(some_var.load(Ordering::Relaxed), 10);
}
