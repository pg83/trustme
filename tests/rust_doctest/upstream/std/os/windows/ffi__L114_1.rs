// Extracted from library/std/src/os/windows/ffi.rs:114
#![allow(unused)]
fn main() {
    use std::ffi::OsString;
    use std::os::windows::prelude::*;
    
    // UTF-16 encoding for "Unicode".
    let source = [0x0055, 0x006E, 0x0069, 0x0063, 0x006F, 0x0064, 0x0065];
    
    let string = OsString::from_wide(&source[..]);
    
    let result: Vec<u16> = string.encode_wide().collect();
    assert_eq!(&source[..], &result[..]);
}
