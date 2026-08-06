// Extracted from library/core/src/char/methods.rs:380
#![allow(unused)]
fn main() {
    assert_eq!('f'.to_digit(10), None);
    assert_eq!('z'.to_digit(16), None);
}
