// Extracted from library/std/src/ffi/os_str.rs:321
#![allow(unused)]
fn main() {
    use std::ffi::OsString;
    
    let mut os_string = OsString::from("foo");
    assert_eq!(&os_string, "foo");
    
    os_string.clear();
    assert_eq!(&os_string, "");
}
