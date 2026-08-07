// Extracted from library/std/src/ffi/os_str.rs:234
#![allow(unused)]
fn main() {
    use std::ffi::OsString;

    let os_string = OsString::from("foo");
    let string = os_string.into_string();
    assert_eq!(string, Ok(String::from("foo")));
}
