// Extracted from library/core/src/str/converts.rs:62
#![allow(unused)]
fn main() {
    use std::str;

    // some invalid bytes, in a vector
    let sparkle_heart = vec![0, 159, 146, 150];

    assert!(str::from_utf8(&sparkle_heart).is_err());
}
