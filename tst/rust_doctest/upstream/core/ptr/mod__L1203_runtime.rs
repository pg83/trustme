// Extracted from library/core/src/ptr/mod.rs:1203
#![allow(unused)]
fn main() {
    use std::ptr;
    let danger: *mut [u8] = ptr::slice_from_raw_parts_mut(ptr::null_mut(), 0);
    unsafe {
        danger.as_mut().expect("references must not be null");
    }
}
