// Extracted from library/alloc/src/string.rs:613
#![allow(unused)]
extern crate alloc;
fn main() {
    // some invalid bytes
    let input = b"Hello \xF0\x90\x80World";
    let output = String::from_utf8_lossy(input);

    assert_eq!("Hello �World", output);
}
