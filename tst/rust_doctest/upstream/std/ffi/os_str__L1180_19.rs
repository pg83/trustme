// Extracted from library/std/src/ffi/os_str.rs:1180
#![allow(unused)]
fn main() {
    use std::ffi::OsString;
    let s = OsString::from("Grüße, Jürgen ❤");

    assert_eq!("grüße, jürgen ❤", s.to_ascii_lowercase());
}
