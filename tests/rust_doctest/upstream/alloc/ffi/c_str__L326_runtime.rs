// Extracted from library/alloc/src/ffi/c_str.rs:326
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::ffi::CString;

    let raw = b"foo".to_vec();
    unsafe {
        let c_string = CString::from_vec_unchecked(raw);
    }
}
