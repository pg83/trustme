// Extracted from library/core/src/sync/atomic.rs:1384
#![allow(unused)]
#![feature(atomic_try_update)]
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};
    
    let x = AtomicBool::new(false);
    assert_eq!(x.try_update(Ordering::SeqCst, Ordering::SeqCst, |_| None), Err(false));
    assert_eq!(x.try_update(Ordering::SeqCst, Ordering::SeqCst, |x| Some(!x)), Ok(false));
    assert_eq!(x.try_update(Ordering::SeqCst, Ordering::SeqCst, |x| Some(!x)), Ok(true));
    assert_eq!(x.load(Ordering::SeqCst), false);
}
