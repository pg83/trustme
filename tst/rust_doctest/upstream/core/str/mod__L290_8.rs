// Extracted from library/core/src/str/mod.rs:290
#![allow(unused)]
fn main() {
    // some bytes, in a vector
    let sparkle_heart = vec![240, 159, 146, 150];

    let sparkle_heart = unsafe {
        str::from_utf8_unchecked(&sparkle_heart)
    };

    assert_eq!("💖", sparkle_heart);
}
