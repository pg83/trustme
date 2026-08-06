// Extracted from library/std/src/ffi/os_str.rs:1241
#![allow(unused)]
fn main() {
    use std::ffi::OsString;
    
    assert!(OsString::from("Ferris").eq_ignore_ascii_case("FERRIS"));
    assert!(OsString::from("Ferrös").eq_ignore_ascii_case("FERRöS"));
    assert!(!OsString::from("Ferrös").eq_ignore_ascii_case("FERRÖS"));
}
