// Extracted from library/core/src/num/int_macros.rs:2781
#![allow(unused)]
fn main() {
    assert_eq!(0x10i32.overflowing_shr(36), (0x1, true));
}
