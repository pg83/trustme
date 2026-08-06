// Extracted from library/core/src/num/uint_macros.rs:2143
#![allow(unused)]
fn main() {
    assert_eq!(10u8.wrapping_mul(12), 120);
    assert_eq!(25u8.wrapping_mul(12), 44);
}
