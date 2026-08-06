// Extracted from library/alloc/src/ffi/c_str.rs:196
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::ffi::CString;
    
    // Some invalid bytes in a vector
    let bytes = b"f\0oo".to_vec();
    
    let value = CString::from_vec_with_nul(bytes.clone());
    
    assert_eq!(bytes, value.unwrap_err().into_bytes());
}
