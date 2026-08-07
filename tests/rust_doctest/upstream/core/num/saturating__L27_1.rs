// Extracted from library/core/src/num/saturating.rs:27
#![allow(unused)]
fn main() {
    use std::num::Saturating;

    let max = Saturating(u32::MAX);
    let one = Saturating(1u32);

    assert_eq!(u32::MAX, (max + one).0);
}
