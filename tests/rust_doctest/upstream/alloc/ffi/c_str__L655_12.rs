// Extracted from library/alloc/src/ffi/c_str.rs:655
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::ffi::CString;
    assert_eq!(
        CString::from_vec_with_nul(b"abc\0".to_vec())
            .expect("CString::from_vec_with_nul failed"),
        c"abc".to_owned()
    );
}
