// Extracted from library/core/src/mem/transmutability.rs:387
#![allow(unused)]
#![feature(transmutability)]
fn main() {
    use core::mem::Assume;

    let assumptions = Assume::ALIGNMENT.and(Assume::SAFETY);
    let to_be_removed = Assume::SAFETY.and(Assume::VALIDITY);

    assert_eq!(
        assumptions.but_not(to_be_removed),
        Assume::ALIGNMENT,
    );
}
