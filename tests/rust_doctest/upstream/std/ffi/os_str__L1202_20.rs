// Extracted from library/std/src/ffi/os_str.rs:1202
#![allow(unused)]
fn main() {
    use std::ffi::OsString;
    let s = OsString::from("Grüße, Jürgen ❤");

    assert_eq!("GRüßE, JüRGEN ❤", s.to_ascii_uppercase());
}
