// Extracted from library/core/src/num/uint_macros.rs:2661
#![allow(unused)]
#![feature(bigint_helper_methods)]
fn main() {
    assert_eq!(5u32.widening_mul(2), (10, 0));
    assert_eq!(1_000_000_000u32.widening_mul(10), (1410065408, 2));
}
