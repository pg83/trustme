// Extracted from library/alloc/src/ffi/c_str.rs:125
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::ffi::{CString, NulError};

    let _: NulError = CString::new(b"f\0oo".to_vec()).unwrap_err();
}
