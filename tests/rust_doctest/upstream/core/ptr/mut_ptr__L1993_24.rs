// Extracted from library/core/src/ptr/mut_ptr.rs:1993
#![allow(unused)]
#![feature(array_ptr_get)]
fn main() {

    let mut arr = [1, 2, 5];
    let ptr: *mut [i32; 3] = &mut arr;
    unsafe {
        (&mut *ptr.as_mut_slice())[..2].copy_from_slice(&[3, 4]);
    }
    assert_eq!(arr, [3, 4, 5]);
}
