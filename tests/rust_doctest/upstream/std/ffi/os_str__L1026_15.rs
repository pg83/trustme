// Extracted from library/std/src/ffi/os_str.rs:1026
#![allow(unused)]
fn main() {
    use std::ffi::OsStr;
    
    let os_str = OsStr::new("");
    assert_eq!(os_str.len(), 0);
    
    let os_str = OsStr::new("foo");
    assert_eq!(os_str.len(), 3);
}
