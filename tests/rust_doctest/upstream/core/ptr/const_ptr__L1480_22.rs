// Extracted from library/core/src/ptr/const_ptr.rs:1480
#![allow(unused)]
#![feature(slice_ptr_get)]
fn main() {
    use std::ptr;
    
    let slice: *const [i8] = ptr::slice_from_raw_parts(ptr::null(), 3);
    assert_eq!(slice.as_ptr(), ptr::null());
}
