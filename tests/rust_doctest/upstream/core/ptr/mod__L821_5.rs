// Extracted from library/core/src/ptr/mod.rs:821
#![allow(unused)]
fn main() {
    use std::ptr;
    
    let p: *const i32 = ptr::null();
    assert!(p.is_null());
    assert_eq!(p as usize, 0); // this pointer has the address 0
}
