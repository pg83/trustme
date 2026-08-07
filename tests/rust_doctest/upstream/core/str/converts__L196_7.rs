// Extracted from library/core/src/str/converts.rs:196
#![allow(unused)]
fn main() {
    use std::str;

    let mut heart = vec![240, 159, 146, 150];
    let heart = unsafe { str::from_utf8_unchecked_mut(&mut heart) };

    assert_eq!("💖", heart);
}
