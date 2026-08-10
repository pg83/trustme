// Extracted from library/core/src/char/methods.rs:1330
#![allow(unused)]
fn main() {
    let mut ascii = 'a';

    ascii.make_ascii_uppercase();

    assert_eq!('A', ascii);
}
