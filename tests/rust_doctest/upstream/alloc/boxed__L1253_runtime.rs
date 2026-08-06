// Extracted from library/alloc/src/boxed.rs:1253
#![allow(unused)]
#![feature(allocator_api, slice_ptr_get)]
extern crate alloc;
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        
        use std::alloc::{Allocator, Layout, System};
        
        unsafe {
            let ptr = System.allocate(Layout::new::<i32>())?.as_mut_ptr() as *mut i32;
            // In general .write is required to avoid attempting to destruct
            // the (uninitialized) previous contents of `ptr`, though for this
            // simple example `*ptr = 5` would have worked as well.
            ptr.write(5);
            let x = Box::from_raw_in(ptr, System);
        }
        Ok::<(), std::alloc::AllocError>(())
    }
    doctest().unwrap();
}
