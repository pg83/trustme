// Extracted from library/core/src/sync/atomic.rs:2664
#![allow(unused)]
fn main() {
    // Get a pointer to an allocated value
    
    
    
    
    {
        // Create an atomic view of the allocated value
    
    
    
        // Use `atomic` for atomic operations, possibly share it with other threads
        atomic.store(1, atomic::Ordering::Relaxed);
    }
    
    // It's ok to non-atomically access the value behind `ptr`,
    // since the reference to the atomic ended its lifetime in the block above
    assert_eq!(unsafe { *ptr }, 1);
    
    // Deallocate the value
    unsafe { drop(Box::from_raw(ptr)) }
}
