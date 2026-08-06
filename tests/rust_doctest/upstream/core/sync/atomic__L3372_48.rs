// Extracted from library/core/src/sync/atomic.rs:3372
#![allow(unused)]
fn main() {
    assert_eq!(x.fetch_update(Ordering::SeqCst, Ordering::SeqCst, |_| None), Err(7));
    assert_eq!(x.fetch_update(Ordering::SeqCst, Ordering::SeqCst, |x| Some(x + 1)), Ok(7));
    assert_eq!(x.fetch_update(Ordering::SeqCst, Ordering::SeqCst, |x| Some(x + 1)), Ok(8));
    assert_eq!(x.load(Ordering::SeqCst), 9);
}
