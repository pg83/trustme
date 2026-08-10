// Extracted from library/core/src/ptr/non_null.rs:1455
#![allow(unused)]
#![feature(slice_ptr_get)]
fn main() {
    use std::ptr::NonNull;

    let slice: NonNull<[i8]> = NonNull::slice_from_raw_parts(NonNull::dangling(), 3);
    assert_eq!(slice.as_mut_ptr(), NonNull::<i8>::dangling().as_ptr());
}
