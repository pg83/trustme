// Extracted from library/std/src/num/f32.rs:953
#![allow(unused)]
fn main() {
    assert_eq!((-1.0_f32).ln_1p(), f32::NEG_INFINITY);
    assert!((-2.0_f32).ln_1p().is_nan());
}
