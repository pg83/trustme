// Extracted from library/core/src/ptr/mut_ptr.rs:1851
#![allow(unused)]
#![feature(slice_ptr_get)]
fn main() {
    use std::ptr;
    
    let slice: *mut [i8] = ptr::slice_from_raw_parts_mut(ptr::null_mut(), 3);
    assert_eq!(slice.as_mut_ptr(), ptr::null_mut());
}
