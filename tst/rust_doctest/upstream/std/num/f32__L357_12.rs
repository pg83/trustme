// Extracted from library/std/src/num/f32.rs:357
#![allow(unused)]
fn main() {
    let positive = 4.0_f32;
    let negative = -4.0_f32;
    let negative_zero = -0.0_f32;

    assert_eq!(positive.sqrt(), 2.0);
    assert!(negative.sqrt().is_nan());
    assert!(negative_zero.sqrt() == negative_zero);
}
