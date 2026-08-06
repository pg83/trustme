// Extracted from library/core/src/sync/atomic.rs:1314
#![allow(unused)]
fn main() {
    use std::sync::atomic::{AtomicBool, Ordering};
    
    let x = AtomicBool::new(false);
    assert_eq!(x.fetch_update(Ordering::SeqCst, Ordering::SeqCst, |_| None), Err(false));
    assert_eq!(x.fetch_update(Ordering::SeqCst, Ordering::SeqCst, |x| Some(!x)), Ok(false));
    assert_eq!(x.fetch_update(Ordering::SeqCst, Ordering::SeqCst, |x| Some(!x)), Ok(true));
    assert_eq!(x.load(Ordering::SeqCst), false);
}
