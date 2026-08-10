// Extracted from library/std/src/num/f32.rs:450
#![allow(unused)]
fn main() {
    assert_eq!(0_f32.ln(), f32::NEG_INFINITY);
    assert!((-42_f32).ln().is_nan());
}
