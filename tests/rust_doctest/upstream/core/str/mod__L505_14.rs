// Extracted from library/core/src/str/mod.rs:505
#![allow(unused)]
fn main() {
    let mut s = String::from("Hello");
    let bytes = unsafe { s.as_bytes_mut() };
    
    assert_eq!(b"Hello", bytes);
}
