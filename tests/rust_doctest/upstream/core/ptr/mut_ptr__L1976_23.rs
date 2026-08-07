// Extracted from library/core/src/ptr/mut_ptr.rs:1976
#![allow(unused)]
#![feature(array_ptr_get)]
fn main() {
    use std::ptr;

    let arr: *mut [i8; 3] = ptr::null_mut();
    assert_eq!(arr.as_mut_ptr(), ptr::null_mut());
}
