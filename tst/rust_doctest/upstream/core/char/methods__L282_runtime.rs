// Extracted from library/core/src/char/methods.rs:282
#![allow(unused)]
fn main() {
    // this panics
    let _c = char::from_digit(1, 37);
}
