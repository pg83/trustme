// Extracted from library/std/src/ffi/os_str.rs:1155
#![allow(unused)]
fn main() {
    use std::ffi::OsString;
    
    let mut s = OsString::from("Grüße, Jürgen ❤");
    
    s.make_ascii_uppercase();
    
    assert_eq!("GRüßE, JüRGEN ❤", s);
}
