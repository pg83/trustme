// Extracted from library/std/src/ffi/os_str.rs:251
#![allow(unused)]
fn main() {
    use std::ffi::OsString;

    let mut os_string = OsString::from("foo");
    os_string.push("bar");
    assert_eq!(&os_string, "foobar");
}
