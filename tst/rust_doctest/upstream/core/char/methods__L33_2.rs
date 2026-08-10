// Extracted from library/core/src/char/methods.rs:33
#![allow(unused)]
fn main() {
    fn something_which_returns_char() -> char { 'a' }
    let c: char = something_which_returns_char();
    assert!(char::MIN <= c);

    let value_at_min = u32::from(char::MIN);
    assert_eq!(char::from_u32(value_at_min), Some('\0'));
}
