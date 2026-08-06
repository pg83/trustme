// Extracted from library/core/src/ffi/c_str.rs:328
#![allow(unused)]
fn main() {
    use std::ffi::CStr;
    
    let cstr = CStr::from_bytes_with_nul(b"hello\0");
    assert_eq!(cstr, Ok(c"hello"));
}
