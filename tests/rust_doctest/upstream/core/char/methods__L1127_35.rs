// Extracted from library/core/src/char/methods.rs:1127
#![allow(unused)]
fn main() {
    assert_eq!('c'.to_uppercase().to_string(), "C");
    
    // Sometimes the result is more than one character:
    assert_eq!('ß'.to_uppercase().to_string(), "SS");
    
    // Characters that do not have both uppercase and lowercase
    // convert into themselves.
    assert_eq!('山'.to_uppercase().to_string(), "山");
}
