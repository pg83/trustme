// Extracted from library/core/src/char/methods.rs:1060
#![allow(unused)]
fn main() {
    assert_eq!('C'.to_lowercase().to_string(), "c");

    // Sometimes the result is more than one character:
    assert_eq!('İ'.to_lowercase().to_string(), "i\u{307}");

    // Characters that do not have both uppercase and lowercase
    // convert into themselves.
    assert_eq!('山'.to_lowercase().to_string(), "山");
}
