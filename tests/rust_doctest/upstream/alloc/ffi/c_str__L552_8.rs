// Extracted from library/alloc/src/ffi/c_str.rs:552
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::ffi::CString;
    
    let c_string = CString::from(c"foo");
    let bytes = c_string.as_bytes_with_nul();
    assert_eq!(bytes, &[b'f', b'o', b'o', b'\0']);
}
