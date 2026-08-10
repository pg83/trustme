// Extracted from library/std/src/num/f32.rs:487
#![allow(unused)]
fn main() {
    assert_eq!(0_f32.log(10.0), f32::NEG_INFINITY);
    assert!((-42_f32).log(10.0).is_nan());
}
