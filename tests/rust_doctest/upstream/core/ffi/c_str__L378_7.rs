// Extracted from library/core/src/ffi/c_str.rs:378
#![allow(unused)]
fn main() {
    use std::ffi::CStr;
    
    let bytes = b"Hello world!\0";
    
    let cstr = unsafe { CStr::from_bytes_with_nul_unchecked(bytes) };
    assert_eq!(cstr.to_bytes_with_nul(), bytes);
}
