// Extracted from library/core/src/slice/raw.rs:261
#![allow(unused)]
#![feature(slice_from_ptr_range)]
fn main() {
    
    use core::slice;
    
    let x = [1, 2, 3];
    let range = x.as_ptr_range();
    
    unsafe {
        assert_eq!(slice::from_ptr_range(range), &x);
    }
}
