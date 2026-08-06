// Extracted from library/alloc/src/string.rs:602
#![allow(unused)]
extern crate alloc;
fn main() {
    // some bytes, in a vector
    let sparkle_heart = vec![240, 159, 146, 150];
    
    let sparkle_heart = String::from_utf8_lossy(&sparkle_heart);
    
    assert_eq!("💖", sparkle_heart);
}
