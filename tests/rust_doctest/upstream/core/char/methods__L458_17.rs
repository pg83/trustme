// Extracted from library/core/src/char/methods.rs:458
#![allow(unused)]
fn main() {
    assert_eq!('❤'.escape_unicode().to_string(), "\\u{2764}");
}
