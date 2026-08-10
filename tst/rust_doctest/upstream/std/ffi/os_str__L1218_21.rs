// Extracted from library/std/src/ffi/os_str.rs:1218
#![allow(unused)]
fn main() {
    use std::ffi::OsString;

    let ascii = OsString::from("hello!\n");
    let non_ascii = OsString::from("Grüße, Jürgen ❤");

    assert!(ascii.is_ascii());
    assert!(!non_ascii.is_ascii());
}
