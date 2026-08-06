// Extracted from library/core/src/char/methods.rs:64
#![allow(unused)]
fn main() {
    fn something_which_returns_char() -> char { 'a' }
    let c: char = something_which_returns_char();
    assert!(c <= char::MAX);
    
    let value_at_max = u32::from(char::MAX);
    assert_eq!(char::from_u32(value_at_max), Some('\u{10FFFF}'));
    assert_eq!(char::from_u32(value_at_max + 1), None);
}
