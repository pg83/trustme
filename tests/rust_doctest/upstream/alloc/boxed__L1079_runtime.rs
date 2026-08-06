// Extracted from library/alloc/src/boxed.rs:1079
#![allow(unused)]
#![feature(box_vec_non_null)]
extern crate alloc;
fn main() {
    
    use std::alloc::{alloc, Layout};
    use std::ptr::NonNull;
    
    unsafe {
        let non_null = NonNull::new(alloc(Layout::new::<i32>()).cast::<i32>())
            .expect("allocation failed");
        // In general .write is required to avoid attempting to destruct
        // the (uninitialized) previous contents of `non_null`.
        non_null.write(5);
        let x = Box::from_non_null(non_null);
    }
}
