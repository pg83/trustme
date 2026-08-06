// Extracted from library/alloc/src/ffi/c_str.rs:1014
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::ffi::CString;
    
    let nul_error = CString::new("foo\0bar").unwrap_err();
    assert_eq!(nul_error.into_vec(), b"foo\0bar");
}
