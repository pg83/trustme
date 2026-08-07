// Extracted from library/alloc/src/string.rs:547
#![allow(unused)]
extern crate alloc;
fn main() {
    // some invalid bytes, in a vector
    let sparkle_heart = vec![0, 159, 146, 150];

    assert!(String::from_utf8(sparkle_heart).is_err());
}
