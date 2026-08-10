// Extracted from library/core/src/str/mod.rs:319
#![allow(unused)]
fn main() {
    let mut heart = vec![240, 159, 146, 150];
    let heart = unsafe { str::from_utf8_unchecked_mut(&mut heart) };

    assert_eq!("💖", heart);
}
