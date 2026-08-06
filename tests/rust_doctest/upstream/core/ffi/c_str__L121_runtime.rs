// Extracted from library/core/src/ffi/c_str.rs:121
#![allow(unused)]
fn main() {
    use std::ffi::{CStr, FromBytesWithNulError};
    
    let _: FromBytesWithNulError = CStr::from_bytes_with_nul(b"f\0oo").unwrap_err();
}
