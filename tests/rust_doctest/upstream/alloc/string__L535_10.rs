// Extracted from library/alloc/src/string.rs:535
#![allow(unused)]
extern crate alloc;
fn main() {
    // some bytes, in a vector
    let sparkle_heart = vec![240, 159, 146, 150];
    
    // We know these bytes are valid, so we'll use `unwrap()`.
    let sparkle_heart = String::from_utf8(sparkle_heart).unwrap();
    
    assert_eq!("💖", sparkle_heart);
}
