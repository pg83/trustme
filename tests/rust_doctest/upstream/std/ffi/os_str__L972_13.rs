// Extracted from library/std/src/ffi/os_str.rs:972
#![allow(unused)]
fn main() {
    use std::ffi::{OsStr, OsString};

    let os_str = OsStr::new("foo");
    let os_string = os_str.to_os_string();
    assert_eq!(os_string, OsString::from("foo"));
}
