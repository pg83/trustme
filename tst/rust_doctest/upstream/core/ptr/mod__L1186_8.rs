// Extracted from library/core/src/ptr/mod.rs:1186
#![allow(unused)]
fn main() {
    use std::ptr;

    let x = &mut [5, 6, 7];
    let raw_pointer = x.as_mut_ptr();
    let slice = ptr::slice_from_raw_parts_mut(raw_pointer, 3);

    unsafe {
        (*slice)[2] = 99; // assign a value at an index in the slice
    };

    assert_eq!(unsafe { &*slice }[2], 99);
}
