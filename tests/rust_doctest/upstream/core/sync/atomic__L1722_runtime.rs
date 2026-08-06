// Extracted from library/core/src/sync/atomic.rs:1722
#![allow(unused)]
fn main() {
    use std::sync::atomic::{AtomicPtr, Ordering};
    
    let ptr = &mut 5;
    let some_ptr = AtomicPtr::new(ptr);
    
    let other_ptr = &mut 10;
    
    some_ptr.store(other_ptr, Ordering::Relaxed);
}
