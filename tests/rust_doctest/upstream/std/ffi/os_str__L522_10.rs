// Extracted from library/std/src/ffi/os_str.rs:522
#![allow(unused)]
fn main() {
    use std::ffi::OsString;
    
    let mut s = OsString::from("foo");
    
    s.reserve(100);
    assert!(s.capacity() >= 100);
    
    s.shrink_to(10);
    assert!(s.capacity() >= 10);
    s.shrink_to(0);
    assert!(s.capacity() >= 3);
}
