// Extracted from library/core/src/ptr/mod.rs:1144
#![allow(unused)]
fn main() {
    use std::ptr;
    
    // create a slice pointer when starting out with a pointer to the first element
    let x = [5, 6, 7];
    let raw_pointer = x.as_ptr();
    let slice = ptr::slice_from_raw_parts(raw_pointer, 3);
    assert_eq!(unsafe { &*slice }[2], 7);
}
