// Extracted from library/core/src/char/methods.rs:799
#![allow(unused)]
fn main() {
    assert!('a'.is_lowercase());
    assert!('δ'.is_lowercase());
    assert!(!'A'.is_lowercase());
    assert!(!'Δ'.is_lowercase());
    
    // The various Chinese scripts and punctuation do not have case, and so:
    assert!(!'中'.is_lowercase());
    assert!(!' '.is_lowercase());
}
