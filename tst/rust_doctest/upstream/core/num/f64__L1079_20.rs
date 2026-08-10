// Extracted from library/core/src/num/f64.rs:1079
#![allow(unused)]
fn main() {
    assert!((1f64).to_bits() != 1f64 as u64); // to_bits() is not casting!
    assert_eq!((12.5f64).to_bits(), 0x4029000000000000);
}
