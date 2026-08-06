// Extracted from library/core/src/num/int_macros.rs:2185
#![allow(unused)]
fn main() {
    assert_eq!((-128i8).wrapping_abs() as u8, 128);
}
