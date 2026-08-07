// Extracted from library/alloc/src/ffi/c_str.rs:994
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::ffi::CString;

    let nul_error = CString::new("foo\0bar").unwrap_err();
    assert_eq!(nul_error.nul_position(), 3);

    let nul_error = CString::new("foo bar\0").unwrap_err();
    assert_eq!(nul_error.nul_position(), 7);
}
