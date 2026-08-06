// Extracted from library/core/src/sync/atomic.rs:1998
#![allow(unused)]
fn main() {
    use std::sync::atomic::{AtomicPtr, Ordering};
    
    let ptr: *mut _ = &mut 5;
    let some_ptr = AtomicPtr::new(ptr);
    
    let new: *mut _ = &mut 10;
    assert_eq!(some_ptr.fetch_update(Ordering::SeqCst, Ordering::SeqCst, |_| None), Err(ptr));
    let result = some_ptr.fetch_update(Ordering::SeqCst, Ordering::SeqCst, |x| {
        if x == ptr {
            Some(new)
        } else {
            None
        }
    });
    assert_eq!(result, Ok(ptr));
    assert_eq!(some_ptr.load(Ordering::SeqCst), new);
}
