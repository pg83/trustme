// Extracted from library/core/src/str/error.rs:64
#![allow(unused)]
fn main() {
    use std::str;
    
    // some invalid bytes, in a vector
    let sparkle_heart = vec![0, 159, 146, 150];
    
    // std::str::from_utf8 returns a Utf8Error
    let error = str::from_utf8(&sparkle_heart).unwrap_err();
    
    // the second byte is invalid here
    assert_eq!(1, error.valid_up_to());
}
