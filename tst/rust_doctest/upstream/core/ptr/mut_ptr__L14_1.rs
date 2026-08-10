// Extracted from library/core/src/ptr/mut_ptr.rs:14
#![allow(unused)]
fn main() {
    let mut s = [1, 2, 3];
    let ptr: *mut u32 = s.as_mut_ptr();
    assert!(!ptr.is_null());
}
