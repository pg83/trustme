// Extracted from library/std/src/ffi/os_str.rs:193
#![allow(unused)]
fn main() {
    use std::ffi::{OsString, OsStr};
    
    let os_string = OsString::from("foo");
    let os_str = OsStr::new("foo");
    assert_eq!(os_string.as_os_str(), os_str);
}
