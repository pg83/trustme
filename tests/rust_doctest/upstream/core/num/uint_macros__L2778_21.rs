// Extracted from library/core/src/num/uint_macros.rs:2778
#![allow(unused)]
#![feature(bigint_helper_methods)]
fn main() {
    assert_eq!(5u32.carrying_mul_add(2, 0, 0), (10, 0));
    assert_eq!(5u32.carrying_mul_add(2, 10, 10), (30, 0));
    assert_eq!(1_000_000_000u32.carrying_mul_add(10, 0, 0), (1410065408, 2));
    assert_eq!(1_000_000_000u32.carrying_mul_add(10, 10, 10), (1410065428, 2));
}
