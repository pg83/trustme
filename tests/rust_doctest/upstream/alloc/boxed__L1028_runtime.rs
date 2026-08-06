// Extracted from library/alloc/src/boxed.rs:1028
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::alloc::{alloc, Layout};
    
    unsafe {
        let ptr = alloc(Layout::new::<i32>()) as *mut i32;
        // In general .write is required to avoid attempting to destruct
        // the (uninitialized) previous contents of `ptr`, though for this
        // simple example `*ptr = 5` would have worked as well.
        ptr.write(5);
        let x = Box::from_raw(ptr);
    }
}
