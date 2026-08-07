// Extracted from library/core/src/ffi/c_str.rs:346
#![allow(unused)]
fn main() {
    use std::ffi::{CStr, FromBytesWithNulError};

    let cstr = CStr::from_bytes_with_nul(b"he\0llo\0");
    assert_eq!(cstr, Err(FromBytesWithNulError::InteriorNul { position: 2 }));
}
