// Extracted from library/std/src/ffi/os_str.rs:342
#![allow(unused)]
fn main() {
    use std::ffi::OsString;
    
    let os_string = OsString::with_capacity(10);
    assert!(os_string.capacity() >= 10);
}
