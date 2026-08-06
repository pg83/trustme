// Extracted from library/alloc/src/ffi/c_str.rs:570
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::ffi::{CString, CStr};
    
    let c_string = CString::from(c"foo");
    let cstr = c_string.as_c_str();
    assert_eq!(cstr,
               CStr::from_bytes_with_nul(b"foo\0").expect("CStr::from_bytes_with_nul failed"));
}
