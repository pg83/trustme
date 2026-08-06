// Extracted from library/core/src/sync/atomic.rs:1139
#![allow(unused)]
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};
    
    let foo = AtomicBool::new(true);
    assert_eq!(foo.fetch_or(false, Ordering::SeqCst), true);
    assert_eq!(foo.load(Ordering::SeqCst), true);
    
    let foo = AtomicBool::new(true);
    assert_eq!(foo.fetch_or(true, Ordering::SeqCst), true);
    assert_eq!(foo.load(Ordering::SeqCst), true);
    
    let foo = AtomicBool::new(false);
    assert_eq!(foo.fetch_or(false, Ordering::SeqCst), false);
    assert_eq!(foo.load(Ordering::SeqCst), false);
}
