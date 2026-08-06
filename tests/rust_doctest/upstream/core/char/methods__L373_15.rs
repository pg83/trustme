// Extracted from library/core/src/char/methods.rs:373
#![allow(unused)]
fn main() {
    assert_eq!('1'.to_digit(10), Some(1));
    assert_eq!('f'.to_digit(16), Some(15));
}
