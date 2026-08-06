// Extracted from library/core/src/sync/atomic.rs:833
#![allow(unused)]
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};
    
    let some_bool = AtomicBool::new(true);
    
    assert_eq!(some_bool.compare_and_swap(true, false, Ordering::Relaxed), true);
    assert_eq!(some_bool.load(Ordering::Relaxed), false);
    
    assert_eq!(some_bool.compare_and_swap(true, true, Ordering::Relaxed), false);
    assert_eq!(some_bool.load(Ordering::Relaxed), false);
}
