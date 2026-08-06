// Extracted from library/core/src/sync/atomic.rs:3438
#![allow(unused)]
#![feature(atomic_try_update)]
fn main() {
    
    
    
    assert_eq!(x.try_update(Ordering::SeqCst, Ordering::SeqCst, |_| None), Err(7));
    assert_eq!(x.try_update(Ordering::SeqCst, Ordering::SeqCst, |x| Some(x + 1)), Ok(7));
    assert_eq!(x.try_update(Ordering::SeqCst, Ordering::SeqCst, |x| Some(x + 1)), Ok(8));
    assert_eq!(x.load(Ordering::SeqCst), 9);
}
