// Extracted from library/std/src/ffi/os_str.rs:901
#![allow(unused)]
fn main() {
    use std::ffi::OsStr;

    let os_str = OsStr::new("foo");
    assert_eq!(os_str.to_str(), Some("foo"));
}
