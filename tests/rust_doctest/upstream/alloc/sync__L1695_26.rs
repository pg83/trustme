// Extracted from library/alloc/src/sync.rs:1695
#![allow(unused)]
#![feature(allocator_api)]
extern crate alloc;
fn main() {
    
    use std::sync::Arc;
    use std::alloc::System;
    
    let x: Arc<[u32], _> = Arc::new_in([1, 2, 3], System);
    let x_ptr: *const [u32] = Arc::into_raw_with_allocator(x).0;
    
    unsafe {
        let x: Arc<[u32; 3], _> = Arc::from_raw_in(x_ptr.cast::<[u32; 3]>(), System);
        assert_eq!(&*x, &[1, 2, 3]);
    }
}
