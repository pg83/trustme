// Extracted from library/core/src/num/int_macros.rs:2492
#![allow(unused)]
fn main() {
    assert_eq!(1_000_000_000i32.overflowing_mul(10), (1410065408, true));
}
