// Extracted from library/std/src/ffi/os_str.rs:545
#![allow(unused)]
fn main() {
    use std::ffi::{OsString, OsStr};
    
    let s = OsString::from("hello");
    
    let b: Box<OsStr> = s.into_boxed_os_str();
}
