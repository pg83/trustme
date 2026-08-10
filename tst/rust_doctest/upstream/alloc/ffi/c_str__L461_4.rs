// Extracted from library/alloc/src/ffi/c_str.rs:461
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::ffi::CString;

    let valid_utf8 = vec![b'f', b'o', b'o'];
    let cstring = CString::new(valid_utf8).expect("CString::new failed");
    assert_eq!(cstring.into_string().expect("into_string() call failed"), "foo");

    let invalid_utf8 = vec![b'f', 0xff, b'o', b'o'];
    let cstring = CString::new(invalid_utf8).expect("CString::new failed");
    let err = cstring.into_string().err().expect("into_string().err() failed");
    assert_eq!(err.utf8_error().valid_up_to(), 1);
}
