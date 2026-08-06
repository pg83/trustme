// Extracted from library/alloc/src/string.rs:2185
#![allow(unused)]
#![feature(string_from_utf8_lossy_owned)]
extern crate alloc;
fn main() {
    // some invalid bytes
    let input: Vec<u8> = b"Hello \xF0\x90\x80World".into();
    let output = String::from_utf8(input).unwrap_or_else(|e| e.into_utf8_lossy());
    
    assert_eq!(String::from("Hello �World"), output);
}
