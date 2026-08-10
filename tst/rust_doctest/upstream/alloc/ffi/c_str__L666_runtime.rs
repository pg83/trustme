// Extracted from library/alloc/src/ffi/c_str.rs:666
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::ffi::{CString, FromVecWithNulError};
    // Interior nul byte
    let _: FromVecWithNulError = CString::from_vec_with_nul(b"a\0bc".to_vec()).unwrap_err();
    // No nul byte
    let _: FromVecWithNulError = CString::from_vec_with_nul(b"abc".to_vec()).unwrap_err();
}
