// Extracted from library/std/src/ffi/os_str.rs:494
#![allow(unused)]
fn main() {
    use std::ffi::OsString;

    let mut s = OsString::from("foo");

    s.reserve(100);
    assert!(s.capacity() >= 100);

    s.shrink_to_fit();
    assert_eq!(3, s.capacity());
}
