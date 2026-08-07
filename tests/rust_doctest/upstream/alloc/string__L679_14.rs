// Extracted from library/alloc/src/string.rs:679
#![allow(unused)]
#![feature(string_from_utf8_lossy_owned)]
extern crate alloc;
fn main() {
    // some invalid bytes
    let input: Vec<u8> = b"Hello \xF0\x90\x80World".into();
    let output = String::from_utf8_lossy_owned(input);

    assert_eq!(String::from("Hello �World"), output);
}
