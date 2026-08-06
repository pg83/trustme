// Extracted from library/std/src/ffi/os_str.rs:992
#![allow(unused)]
fn main() {
    use std::ffi::OsStr;
    
    let os_str = OsStr::new("");
    assert!(os_str.is_empty());
    
    let os_str = OsStr::new("foo");
    assert!(!os_str.is_empty());
}
