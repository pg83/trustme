// Extracted from library/core/src/sync/atomic.rs:3500
#![allow(unused)]
#![feature(atomic_try_update)]
fn main() {
    
    
    
    assert_eq!(x.update(Ordering::SeqCst, Ordering::SeqCst, |x| x + 1), 7);
    assert_eq!(x.update(Ordering::SeqCst, Ordering::SeqCst, |x| x + 1), 8);
    assert_eq!(x.load(Ordering::SeqCst), 9);
}
