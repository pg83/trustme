// Extracted from library/core/src/ffi/c_str.rs:337
#![allow(unused)]
fn main() {
    use std::ffi::{CStr, FromBytesWithNulError};
    
    let cstr = CStr::from_bytes_with_nul(b"hello");
    assert_eq!(cstr, Err(FromBytesWithNulError::NotNulTerminated));
}
