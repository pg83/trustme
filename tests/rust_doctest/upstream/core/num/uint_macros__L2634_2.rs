// Extracted from library/core/src/num/uint_macros.rs:2634
#![allow(unused)]
fn main() {
    assert_eq!(5u32.overflowing_mul(2), (10, false));
    assert_eq!(1_000_000_000u32.overflowing_mul(10), (1410065408, true));
}
