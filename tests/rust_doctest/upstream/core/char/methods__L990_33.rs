// Extracted from library/core/src/char/methods.rs:990
#![allow(unused)]
fn main() {
    assert!('٣'.is_numeric());
    assert!('7'.is_numeric());
    assert!('৬'.is_numeric());
    assert!('¾'.is_numeric());
    assert!('①'.is_numeric());
    assert!(!'K'.is_numeric());
    assert!(!'و'.is_numeric());
    assert!(!'藏'.is_numeric());
    assert!(!'三'.is_numeric());
}
