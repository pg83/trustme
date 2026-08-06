// Extracted from library/alloc/src/sync.rs:1435
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::sync::Arc;
    
    let x = Arc::new("hello".to_owned());
    let x_ptr = Arc::into_raw(x);
    
    unsafe {
        // Convert back to an `Arc` to prevent leak.
        let x = Arc::from_raw(x_ptr);
        assert_eq!(&*x, "hello");
    
        // Further calls to `Arc::from_raw(x_ptr)` would be memory-unsafe.
    }
    
    // The memory was freed when `x` went out of scope above, so `x_ptr` is now dangling!
}
