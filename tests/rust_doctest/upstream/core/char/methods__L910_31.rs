// Extracted from library/core/src/char/methods.rs:910
#![allow(unused)]
fn main() {
    assert!('٣'.is_alphanumeric());
    assert!('7'.is_alphanumeric());
    assert!('৬'.is_alphanumeric());
    assert!('¾'.is_alphanumeric());
    assert!('①'.is_alphanumeric());
    assert!('K'.is_alphanumeric());
    assert!('و'.is_alphanumeric());
    assert!('藏'.is_alphanumeric());
}
