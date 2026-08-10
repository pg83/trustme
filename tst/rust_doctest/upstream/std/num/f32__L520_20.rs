// Extracted from library/std/src/num/f32.rs:520
#![allow(unused)]
fn main() {
    assert_eq!(0_f32.log2(), f32::NEG_INFINITY);
    assert!((-42_f32).log2().is_nan());
}
