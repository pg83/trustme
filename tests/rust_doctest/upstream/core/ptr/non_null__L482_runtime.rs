// Extracted from library/core/src/ptr/non_null.rs:482
#![allow(unused)]
fn main() {
    use std::ptr::NonNull;

    let mut x = 0u32;
    let ptr = NonNull::new(&mut x as *mut _).expect("null pointer");

    let casted_ptr = ptr.cast::<i8>();
    let raw_ptr: *mut i8 = casted_ptr.as_ptr();
}
