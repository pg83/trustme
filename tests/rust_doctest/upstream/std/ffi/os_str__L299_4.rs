// Extracted from library/std/src/ffi/os_str.rs:299
#![allow(unused)]
fn main() {
    use std::ffi::OsString;

    let mut os_string = OsString::with_capacity(10);
    let capacity = os_string.capacity();

    // This push is done without reallocating
    os_string.push("foo");

    assert_eq!(capacity, os_string.capacity());
}
