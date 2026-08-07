// Extracted from library/std/src/ffi/os_str.rs:431
#![allow(unused)]
fn main() {
    use std::ffi::OsString;

    let mut s = OsString::new();
    s.reserve_exact(10);
    assert!(s.capacity() >= 10);
}
