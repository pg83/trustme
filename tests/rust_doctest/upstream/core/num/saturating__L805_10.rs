// Extracted from library/core/src/num/saturating.rs:805
#![allow(unused)]
fn main() {
    use std::num::Saturating;
    
    assert_eq!(Saturating(3i8).pow(5), Saturating(127));
    assert_eq!(Saturating(3i8).pow(6), Saturating(127));
}
