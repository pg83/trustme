// Extracted from library/core/src/sync/atomic.rs:537
#![allow(unused)]
fn main() {
    use std::sync::atomic::{self, AtomicBool};
    
    // Get a pointer to an allocated value
    let ptr: *mut bool = Box::into_raw(Box::new(false));
    
    assert!(ptr.cast::<AtomicBool>().is_aligned());
    
    {
        // Create an atomic view of the allocated value
        let atomic = unsafe { AtomicBool::from_ptr(ptr) };
    
        // Use `atomic` for atomic operations, possibly share it with other threads
        atomic.store(true, atomic::Ordering::Relaxed);
    }
    
    // It's ok to non-atomically access the value behind `ptr`,
    // since the reference to the atomic ended its lifetime in the block above
    assert_eq!(unsafe { *ptr }, true);
    
    // Deallocate the value
    unsafe { drop(Box::from_raw(ptr)) }
}
