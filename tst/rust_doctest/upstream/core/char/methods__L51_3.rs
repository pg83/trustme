// Extracted from library/core/src/char/methods.rs:51
#![allow(unused)]
fn main() {
    let dist = u32::from(char::MAX) - u32::from(char::MIN);
    let size = (char::MIN..=char::MAX).count() as u32;
    assert!(size < dist);
}
