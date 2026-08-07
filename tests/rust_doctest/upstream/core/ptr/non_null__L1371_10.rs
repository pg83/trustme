// Extracted from library/core/src/ptr/non_null.rs:1371
#![allow(unused)]
fn main() {
    use std::ptr::NonNull;

    // create a slice pointer when starting out with a pointer to the first element
    let mut x = [5, 6, 7];
    let nonnull_pointer = NonNull::new(x.as_mut_ptr()).unwrap();
    let slice = NonNull::slice_from_raw_parts(nonnull_pointer, 3);
    assert_eq!(unsafe { slice.as_ref()[2] }, 7);
}
