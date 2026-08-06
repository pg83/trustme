// Extracted from library/core/src/char/methods.rs:322
#![allow(unused)]
fn main() {
    assert!('1'.is_digit(10));
    assert!('f'.is_digit(16));
    assert!(!'f'.is_digit(10));
}
