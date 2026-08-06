// Extracted from library/core/src/ffi/c_str.rs:239
#![allow(unused)]
fn main() {
    use std::ffi::{c_char, CStr};
    
    const HELLO_PTR: *const c_char = {
        const BYTES: &[u8] = b"Hello, world!\0";
        BYTES.as_ptr().cast()
    };
    const HELLO: &CStr = unsafe { CStr::from_ptr(HELLO_PTR) };
    
    assert_eq!(c"Hello, world!", HELLO);
}
