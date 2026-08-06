// Extracted from library/std/src/ffi/os_str.rs:365
#![allow(unused)]
fn main() {
    use std::ffi::OsString;
    
    let mut s = OsString::new();
    s.reserve(10);
    assert!(s.capacity() >= 10);
}
