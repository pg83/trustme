// Extracted from library/core/src/sync/atomic.rs:1855
#![allow(unused)]
fn main() {
    use std::sync::atomic::{AtomicPtr, Ordering};

    let ptr = &mut 5;
    let some_ptr = AtomicPtr::new(ptr);

    let other_ptr = &mut 10;

    let value = some_ptr.compare_exchange(ptr, other_ptr,
                                          Ordering::SeqCst, Ordering::Relaxed);
}
