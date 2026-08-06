// Extracted from library/core/src/num/uint_macros.rs:2744
#![allow(unused)]
#![feature(bigint_helper_methods)]
fn main() {
    assert_eq!(
        789_u16.carrying_mul(456, 123).0,
        789_u16.wrapping_mul(456).wrapping_add(123),
    );
}
