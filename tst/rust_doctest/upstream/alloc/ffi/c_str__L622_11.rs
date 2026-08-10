// Extracted from library/alloc/src/ffi/c_str.rs:622
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::ffi::CString;
    assert_eq!(
        unsafe { CString::from_vec_with_nul_unchecked(b"abc\0".to_vec()) },
        unsafe { CString::from_vec_unchecked(b"abc".to_vec()) }
    );
}
