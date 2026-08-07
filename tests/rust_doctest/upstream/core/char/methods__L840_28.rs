// Extracted from library/core/src/char/methods.rs:840
#![allow(unused)]
fn main() {
    assert!(!'a'.is_uppercase());
    assert!(!'δ'.is_uppercase());
    assert!('A'.is_uppercase());
    assert!('Δ'.is_uppercase());

    // The various Chinese scripts and punctuation do not have case, and so:
    assert!(!'中'.is_uppercase());
    assert!(!' '.is_uppercase());
}
