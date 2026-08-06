// Extracted from library/core/src/num/int_macros.rs:2759
#![allow(unused)]
fn main() {
    assert_eq!(0x1i32.overflowing_shl(36), (0x10, true));
}
