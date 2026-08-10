// Extracted from library/core/src/char/methods.rs:261
#![allow(unused)]
fn main() {
    let c = char::from_digit(4, 10);

    assert_eq!(Some('4'), c);

    // Decimal 11 is a single digit in base 16
    let c = char::from_digit(11, 16);

    assert_eq!(Some('b'), c);
}
