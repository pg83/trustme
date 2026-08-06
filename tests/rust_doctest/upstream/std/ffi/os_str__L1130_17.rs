// Extracted from library/std/src/ffi/os_str.rs:1130
#![allow(unused)]
fn main() {
    use std::ffi::OsString;
    
    let mut s = OsString::from("GRÜßE, JÜRGEN ❤");
    
    s.make_ascii_lowercase();
    
    assert_eq!("grÜße, jÜrgen ❤", s);
}
