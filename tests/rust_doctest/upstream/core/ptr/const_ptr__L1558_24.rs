// Extracted from library/core/src/ptr/const_ptr.rs:1558
#![allow(unused)]
#![feature(array_ptr_get)]
fn main() {
    use std::ptr;

    let arr: *const [i8; 3] = ptr::null();
    assert_eq!(arr.as_ptr(), ptr::null());
}
