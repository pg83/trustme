// Extracted from library/core/src/ptr/const_ptr.rs:1444
#![allow(unused)]
fn main() {
    use std::ptr;
    
    let slice: *const [i8] = ptr::slice_from_raw_parts(ptr::null(), 3);
    assert_eq!(slice.len(), 3);
}
