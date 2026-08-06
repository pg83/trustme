// Extracted from library/alloc/src/string.rs:31
#![allow(unused)]
extern crate alloc;
fn main() {
    let sparkle_heart = vec![240, 159, 146, 150];
    
    // We know these bytes are valid, so we'll use `unwrap()`.
    let sparkle_heart = String::from_utf8(sparkle_heart).unwrap();
    
    assert_eq!("💖", sparkle_heart);
    
    let bytes = sparkle_heart.into_bytes();
    
    assert_eq!(bytes, [240, 159, 146, 150]);
}
