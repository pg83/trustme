// Extracted from library/core/src/str/mod.rs:228
#![allow(unused)]
fn main() {
    // some bytes, in a stack-allocated array
    let sparkle_heart = [240, 159, 146, 150];
    
    // We know these bytes are valid, so just use `unwrap()`.
    let sparkle_heart: &str = str::from_utf8(&sparkle_heart).unwrap();
    
    assert_eq!("💖", sparkle_heart);
}
