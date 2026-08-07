// Extracted from library/core/src/ptr/mut_ptr.rs:1701
#![allow(unused)]
fn main() {
    use std::ptr;

    let slice: *mut [i8] = ptr::slice_from_raw_parts_mut(ptr::null_mut(), 3);
    assert_eq!(slice.len(), 3);
}
