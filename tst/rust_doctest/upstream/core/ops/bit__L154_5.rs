// Extracted from library/core/src/ops/bit.rs:154
#![allow(unused)]
fn main() {
    assert_eq!(true & false, false);
    assert_eq!(true & true, true);
    assert_eq!(5u8 & 1u8, 1);
    assert_eq!(5u8 & 2u8, 0);
}
