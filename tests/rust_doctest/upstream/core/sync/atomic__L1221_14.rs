// Extracted from library/core/src/sync/atomic.rs:1221
#![allow(unused)]
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};
    
    let foo = AtomicBool::new(true);
    assert_eq!(foo.fetch_not(Ordering::SeqCst), true);
    assert_eq!(foo.load(Ordering::SeqCst), false);
    
    let foo = AtomicBool::new(false);
    assert_eq!(foo.fetch_not(Ordering::SeqCst), false);
    assert_eq!(foo.load(Ordering::SeqCst), true);
}
