// Extracted from library/core/src/sync/atomic.rs:2146
#![allow(unused)]
#![feature(atomic_try_update)]
fn main() {

    use std::sync::atomic::{AtomicPtr, Ordering};

    let ptr: *mut _ = &mut 5;
    let some_ptr = AtomicPtr::new(ptr);

    let new: *mut _ = &mut 10;
    let result = some_ptr.update(Ordering::SeqCst, Ordering::SeqCst, |_| new);
    assert_eq!(result, ptr);
    assert_eq!(some_ptr.load(Ordering::SeqCst), new);
}
