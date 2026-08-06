// Extracted from library/alloc/src/string.rs:667
#![allow(unused)]
#![feature(string_from_utf8_lossy_owned)]
extern crate alloc;
fn main() {
    // some bytes, in a vector
    let sparkle_heart = vec![240, 159, 146, 150];
    
    let sparkle_heart = String::from_utf8_lossy_owned(sparkle_heart);
    
    assert_eq!(String::from("💖"), sparkle_heart);
}
