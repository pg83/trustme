// Extracted from library/std/src/ffi/os_str.rs:1451
#![allow(unused)]
fn main() {
    use std::ffi::OsStr;
    
    let os_str = OsStr::new("foo");
    let as_str = <&str>::try_from(os_str).unwrap();
    assert_eq!(as_str, "foo");
}
