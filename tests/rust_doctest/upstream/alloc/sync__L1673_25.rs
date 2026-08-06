// Extracted from library/alloc/src/sync.rs:1673
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    
    use std::sync::Arc;
    use std::alloc::System;
    
    let x = Arc::new_in("hello".to_owned(), System);
    let (x_ptr, alloc) = Arc::into_raw_with_allocator(x);
    
    unsafe {
        // Convert back to an `Arc` to prevent leak.
        let x = Arc::from_raw_in(x_ptr, System);
        assert_eq!(&*x, "hello");
    
        // Further calls to `Arc::from_raw(x_ptr)` would be memory-unsafe.
    }
    
    // The memory was freed when `x` went out of scope above, so `x_ptr` is now dangling!
}
