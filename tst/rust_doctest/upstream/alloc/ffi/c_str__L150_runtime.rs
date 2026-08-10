// Extracted from library/alloc/src/ffi/c_str.rs:150
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::ffi::{CString, FromVecWithNulError};

    let _: FromVecWithNulError = CString::from_vec_with_nul(b"f\0oo".to_vec()).unwrap_err();
}
