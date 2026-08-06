// Extracted from library/core/src/sync/atomic.rs:1495
#![allow(unused)]
fn main() {
    use std::sync::atomic::{self, AtomicPtr};
    
    // Get a pointer to an allocated value
    let ptr: *mut *mut u8 = Box::into_raw(Box::new(std::ptr::null_mut()));
    
    assert!(ptr.cast::<AtomicPtr<u8>>().is_aligned());
    
    {
        // Create an atomic view of the allocated value
        let atomic = unsafe { AtomicPtr::from_ptr(ptr) };
    
        // Use `atomic` for atomic operations, possibly share it with other threads
        atomic.store(std::ptr::NonNull::dangling().as_ptr(), atomic::Ordering::Relaxed);
    }
    
    // It's ok to non-atomically access the value behind `ptr`,
    // since the reference to the atomic ended its lifetime in the block above
    assert!(!unsafe { *ptr }.is_null());
    
    // Deallocate the value
    unsafe { drop(Box::from_raw(ptr)) }
}
