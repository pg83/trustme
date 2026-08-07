// Extracted from library/core/src/char/methods.rs:274
#![allow(unused)]
fn main() {
    let c = char::from_digit(20, 10);

    assert_eq!(None, c);
}
