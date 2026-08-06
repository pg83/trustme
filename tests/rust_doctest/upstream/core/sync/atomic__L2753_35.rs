// Extracted from library/core/src/sync/atomic.rs:2753
#![allow(unused)]
#![feature(atomic_from_mut)]
fn main() {
    
    
    let mut some_int = 123;
    
    a.store(100, Ordering::Relaxed);
    assert_eq!(some_int, 100);
}
