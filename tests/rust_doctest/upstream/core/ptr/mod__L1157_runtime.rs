// Extracted from library/core/src/ptr/mod.rs:1157
#![allow(unused)]
fn main() {
    use std::ptr;
    let danger: *const [u8] = ptr::slice_from_raw_parts(ptr::null(), 0);
    unsafe {
        danger.as_ref().expect("references must not be null");
    }
}
