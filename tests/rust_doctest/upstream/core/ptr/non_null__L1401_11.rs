// Extracted from library/core/src/ptr/non_null.rs:1401
#![allow(unused)]
fn main() {
    use std::ptr::NonNull;
    
    let slice: NonNull<[i8]> = NonNull::slice_from_raw_parts(NonNull::dangling(), 3);
    assert_eq!(slice.len(), 3);
}
