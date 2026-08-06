// Extracted from library/core/src/iter/sources/successors.rs:16
#![allow(unused)]
fn main() {
    use std::iter::successors;
    
    let powers_of_10 = successors(Some(1_u16), |n| n.checked_mul(10));
    assert_eq!(powers_of_10.collect::<Vec<_>>(), &[1, 10, 100, 1_000, 10_000]);
}
