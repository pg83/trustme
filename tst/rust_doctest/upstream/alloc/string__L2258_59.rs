// Extracted from library/alloc/src/string.rs:2258
#![allow(unused)]
extern crate alloc;
fn main() {
    // some invalid bytes, in a vector
    let bytes = vec![0, 159];

    let error = String::from_utf8(bytes).unwrap_err().utf8_error();

    // the first byte is invalid here
    assert_eq!(1, error.valid_up_to());
}
