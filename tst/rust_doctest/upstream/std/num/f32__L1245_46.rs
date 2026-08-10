// Extracted from library/std/src/num/f32.rs:1245
#![allow(unused)]
#![feature(float_erf)]
fn main() {
    let x: f32 = 0.123;

    let one = x.erf() + x.erfc();
    let abs_difference = (one - 1.0).abs();

    assert!(abs_difference <= f32::EPSILON);
}
