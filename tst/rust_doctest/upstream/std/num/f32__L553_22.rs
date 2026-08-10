// Extracted from library/std/src/num/f32.rs:553
#![allow(unused)]
fn main() {
    assert_eq!(0_f32.log10(), f32::NEG_INFINITY);
    assert!((-42_f32).log10().is_nan());
}
