// Extracted from library/core/src/ptr/mod.rs:846
#![allow(unused)]
fn main() {
    use std::ptr;
    
    let p: *mut i32 = ptr::null_mut();
    assert!(p.is_null());
    assert_eq!(p as usize, 0); // this pointer has the address 0
}
