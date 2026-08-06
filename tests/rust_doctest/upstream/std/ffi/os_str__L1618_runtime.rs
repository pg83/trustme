// Extracted from library/std/src/ffi/os_str.rs:1618
#![allow(unused)]
fn main() {
    use std::ffi::OsStr;
    
    let s = OsStr::new("Hello, world!");
    println!("{}", s.display());
}
