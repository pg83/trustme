// Extracted from library/core/src/ffi/c_str.rs:472
#![allow(unused)]
fn main() {
    use std::ffi::{CStr, CString};

    let c_str = CString::new("Hi!".to_uppercase()).unwrap();
    let ptr = c_str.as_ptr();

    assert_eq!(unsafe { CStr::from_ptr(ptr) }, c"HI!");
}
