// Extracted from library/alloc/src/boxed.rs:1306
#![allow(unused)]
#![feature(allocator_api, box_vec_non_null, slice_ptr_get)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        use std::alloc::{Allocator, Layout, System};
        
        unsafe {
            let non_null = System.allocate(Layout::new::<i32>())?.cast::<i32>();
            // In general .write is required to avoid attempting to destruct
            // the (uninitialized) previous contents of `non_null`.
            non_null.write(5);
            let x = Box::from_non_null_in(non_null, System);
        }
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
