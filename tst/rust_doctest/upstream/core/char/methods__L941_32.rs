// Extracted from library/core/src/char/methods.rs:941
#![allow(unused)]
fn main() {
    // U+009C, STRING TERMINATOR
    assert!(''.is_control());
    assert!(!'q'.is_control());
}
